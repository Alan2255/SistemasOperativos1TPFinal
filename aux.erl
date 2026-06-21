-module(aux).
-export([eliminar_indice/2, inicializar_sistema/2, handler_job/6, wait_jobs/1, supervisor_scheduler_jobs/3]).

%========= Funciones AUXILIARES  ===============$
%MapNodos : mapa donde key es el nodo y value lista con 3 enteros, donde cada entero representa en orden la cantidad de CPU, MEM, GPU

obtener_cant_maxima_recursos([], ListMaximos) ->
    ListMaximos;

%Obtiene la cantidad maxima de cada recurso entre todos los nodos disponibles, sirve para armar los jobs sin que se pase del maximo q puede obtener
obtener_cant_maxima_recursos([Nodo | Resto], ListMaximos) -> %Recibe una lista donde cada elemento es un string con el nodo con sus datos y otra lista con 3 int
    List_recursos = string:tokens(Nodo, ":"), %devuelve una lista con cda elem del nodo, ej [host, puerto, cpu, cntcpu, mem, cntmem, etc
    [MaxCPU, MaxMEM, MaxGPU] = ListMaximos,
    case List_recursos of 
        [_Host, _Puerto , "cpu", CantCPU,  "mem", CantMEM, "gpu", CantGPU] -> 
            SumaCPU = MaxCPU + list_to_integer(CantCPU),
            SumaMEM = MaxMEM + list_to_integer(CantMEM),
            SumaGPU = MaxGPU + list_to_integer(CantGPU),
            obtener_cant_maxima_recursos(Resto, [SumaCPU, SumaMEM, SumaGPU]);

        _ ->
            %si no pudo asignar es pq esta mal el nodo
            io:format("Nodo mal formado!~n"),
            throw(badmatch) %Si mandaron datos erroneos desde C, terminamos el programa pq no podra funcionar
    end.
          
%Split devuelve: primera lista con los primeros k elem y la segunda lista el resto ej (3, [a,b,c,d]) devolvera
% [a,b,c] [d], como queres borrar el elemento N, haces N-1 para q el elem q qres borrar quede al inicio de la segunda lista
% entonces haces {izq, [ _ | Der]} q ignora el primer elemento y desp unis toda la lista ignorando ese elem
eliminar_indice(N, Lista) ->%N(int), Lista(Lista de 3 int)
    {Izq, [_ | Der]} = lists:split(N - 1, Lista), 
    Izq ++ Der.  

%Recibe un nodo string con los datos y retorna una lista con nodo CANTCPU CANT MEM CANTGPU       
% string:toekns ":" , devuelve una lista con cda elem del nodo, ej [host, puerto, cpu, cntcpu, mem, cntmem, 
parsear_un_nodo(Nodo) -> %Nodo(string)
    [Host, _Puerto, "cpu", CantCPU, "mem", CantMEM, "gpu", CantGPU] = string:tokens(Nodo, ":"),%luego aplicamos Patter Matching
    {Host, [list_to_integer(CantCPU), list_to_integer(CantMEM), list_to_integer(CantGPU)]}.

%aplica a cada nodo la funcion anterior, devuelve la lista con sublistas formadas por lo q devuelve la fun, para desp transformarlo en un mapa
% entonces podemos acceder a nodo tal y a la cant de su cpu,mem, gpu
parsear_lista_nodos(ListNodos) -> %ListNodos(list de strings)
    maps:from_list([parsear_un_nodo(Nodo) || Nodo <- ListNodos]).


repartir_entre_nodos(_Indice, 0, _Nodos) -> 
    [];

%SI sigue habiendo cantidad distinta de 0 y ya recorrio toda la lista entonces no alcanzó entre todos los nodos
repartir_entre_nodos(_Indice, _CantidadRestante, []) ->
    {error, no_alcanza};

%Devuelve una lista con los nodos a los cuales pedir y cuanto le pide a cada uno
repartir_entre_nodos(Indice, CantidadRestante, [{Host, Recursos} | Resto]) -> %Indice(int), CantidadRestante(int), Lista de tuplas
    Disponible = lists:nth(Indice, Recursos),
    case Disponible of
        0 -> %SI no tiene nada dispoible nos fijamos en el prox nodo(resto llama al prox nodo y de vuelta se divide entre primer elemento y resto la lista)
            repartir_entre_nodos(Indice, CantidadRestante, Resto);
        _ -> %SI hay cantidad para tomar
            Tomar = min(Disponible, CantidadRestante),%El minimo entre cant q queres o la cantidad disponible, para no sobrepasarte
            [{Host, Tomar} | repartir_entre_nodos(Indice, CantidadRestante - Tomar, Resto)]%Agrega el elem a la lista y llama recursivamente restando lo tomado de la cantidad
    end.    

%En el case busca en el mapa de nodos el primer nodo q tenga suficiente recurso segun el tipo de recurso pedido y su cant requerida
%%Devuelve una lista con los nodos a los cuales pedir y cuanto le pide a cada uno
%Si encuentra en 1 pasa lo de true y sino tiene q buscar entre mas nodos para completar
elegir_nodos(Recurso, Cantidad, MapNodos) -> %Recurso(string), Cantidad(int), MapNodos(map)
    Nodos = maps:to_list(MapNodos), %convierte mapa en una lista d tuplas EJ :[{Nodo1, [CPU, MEM, GPU]}, {Nodo2, [CPU, MEM, GPU]}] etc
    Indice = case Recurso of%convierte el recurso pedido en un indice de la lista de recursos
        "cpu" -> 1;
        "mem" -> 2;
        "gpu" -> 3
    end,
    case lists:search(fun({_Host, Recursos}) ->%Busca el primer nodo q cumpla la condicion de q la cant del recurso del nodo sea mayorigual a la q necesitamos
        lists:nth(Indice, Recursos) >= list_to_integer(Cantidad) 
    end,
     Nodos) of  %Si lo encontro devuelve {value, {Nodo, Recursos}, retornamos Nodo y Cantidad 
        {value, {Nodo, _}} -> 
            [{Nodo, Cantidad}]; %encontramos 1 solo nodo q cubre toda la cantidad requerida, devolvemos la Cantidad pq el recurso ya lo sabemos

        false -> %hay q pedir entre varios nodos
            repartir_entre_nodos(Indice, Cantidad, Nodos)%Si no lo encontro reetornamos error no hay nodo
    end.
    
%Funcion que devuelve el msg armado con el job, luego solo faltaria agregarle la peticion y el JobID.
armar_msg(Recurso, Cant, MapNodos) -> %Recurso(string), Cant(string), MapNodos(map)
    case elegir_nodos(Recurso, list_to_integer(Cant), MapNodos) of 
        {error, no_alcanza} -> %devuelve esto si no alcanzaron los nodos para la cantidad q vos querias
            {error, no_alcanza};

        ListNodoCantidad -> %devuelve la lista de tuplas [{Nodo1, CantidadTomada}, {Nodo2, CantidadTomada2} 
            ListPedidoPorNodo = ["@" ++ Nodo ++ ":" ++ Recurso ++ ":" ++ integer_to_list(Cantidad) || {Nodo, Cantidad} <- ListNodoCantidad ],%A cada elem de la lista, le aplica eso
            %Devuelve una lista con ["@Nodo:Recurso:Cant", "@Nodo:Recurso:Cant", etc]
            string:join(ListPedidoPorNodo, " ")%retorna un string donde separa cada elem de la lista con un " ", EJ: @Nodo:Recurso:Cant @Nodo:Recurso:Cant etc
    end.

%Maneja el Job, lo arma el msg final para enviar al agente.
handler_msgs(JobID, Job, CantRecursos, MapNodos) -> %JobID(string), Job(string),CantRecursos(int) MapNodos(mapa)
    List_recursos = string:tokens(Job, ":"),
    case CantRecursos of 
        1 ->
            [Recurso1, Cant1] = List_recursos,
            case armar_msg(Recurso1, Cant1, MapNodos) of 
                {error, no_alcanza} ->  %Primero evaluamos el error, pq sino Msg al ser variable matchea cualquier cosa que llegue
                    {error, no_alcanza};
                Msg -> %Msg es un string por ej: @Nodo:Recurso:Cant @Nodo:Recurso:Cant etc. 
                    Msg_final = "JOB_REQUEST" ++ " " ++ JobID ++ Msg,
                    {JobID, Job, Msg_final}
            end;

        2 ->
            [Recurso1, Cant1, Recurso2, Cant2] = List_recursos,
            case armar_msg(Recurso1, Cant1, MapNodos) of
                {error, no_alcanza} -> 
                    {error, no_alcanza};
                Msg1 ->
                    case armar_msg(Recurso2, Cant2, MapNodos) of
                        {error, no_alcanza} -> 
                            {error, no_alcanza};
                        Msg2 ->
                            Msg_final = "JOB_REQUEST" ++ " " ++ JobID ++ Msg1 ++ Msg2,
                            {JobID, Job, Msg_final}
                    end
            end;

        3 ->
            [Recurso1, Cant1, Recurso2, Cant2, Recurso3, Cant3] = List_recursos,
            case armar_msg(Recurso1, Cant1, MapNodos) of
                {error, no_alcanza} -> 
                    {error, no_alcanza};
                Msg1 ->
                    case armar_msg(Recurso2, Cant2, MapNodos) of
                        {error, no_alcanza} -> 
                            {error, no_alcanza};
                        Msg2 ->
                            case armar_msg(Recurso3, Cant3, MapNodos) of
                                {error, no_alcanza} -> 
                                    {error, no_alcanza};
                                Msg3 ->
                                    Msg_final = "JOB_REQUEST" ++ " " ++ JobID ++ Msg1 ++ Msg2 ++ Msg3,
                                    {JobID, Job, Msg_final}
                            end
                    end
            end
    end.


%Si no existe lo crea y sino escribe al final
% Funcion que registra en un archivo log la fecha, hora, job id, job y respuesta del agente.
registrar_log(JobID, Job, Msg_resultado)-> %JobID(string), Job(string), Msg_Resultado(string)
    {{Y,M,D}, {H,Mi,S}} = calendar:local_time(), %obtiene la fecha 
    Linea = io_lib:format("~p-~p-~p ~p:~p:~p | Job ~p | ~s | ~s~n", [Y, M, D, H, Mi, S, JobID, Job, Msg_resultado]), %Devuelve un string para usarlo, a dif de io:format que imprime directo en la consola
    file:write_file("scheduler.log", Linea , [append]). %append para q no se borre lo anterior

%Borra el Job de la tabla de pendientes y registra su log.
borrarPendiente_and_registrarLog(JobID, Job, Msg) -> %JobID(string), Job(string), Msg(String)
    ets:delete(pendientes, JobID), %lo elimino de la lista de pendientes
    registrar_log(JobID, Job, Msg).

%Arma las peticiones que mandara a C
armar_peticiones(JobID, Job, CantRecursos, MapNodos) -> %%JobID(string), Job(string), CantRecursos(int), MapNodos(map)
    case handler_msgs(JobID, Job, CantRecursos, MapNodos) of %Devuelve el msg completo para enviar a si puede y sino error.
        {error, no_alcanza} ->
            {error, no_alcanza};
         Msg_REQUEST ->
            Msg_RELEASE = "JOB_RELEASE" ++ JobID, %Liberar recurso
            {Msg_REQUEST, Msg_RELEASE}
    end.

%Se intenta conectar al socket, si es exitosa, envia msg al socket con get nodes, recibe y crea el mapa con nodos y sus cantidades
conectar_y_obtener_nodos(Puerto) ->
    case gen_tcp:connect("localhost", Puerto, [binary, {packet, 2}]) of 
        {ok, Socket} ->
            gen_tcp:send(Socket, <<"GET_NODES\n">>), %consulto con el agente C, me respondera con una lista de nodos vivos en formato de texto, EJ: NODES 192.168.1.10:8100:cpu:4:mem:8192:gpu:1 
            {ok, BinList} = gen_tcp:recv(Socket, 0),%por mas q diga lista lor recibo como un binario q luego transformo a string

            List_nodos_separados = string:split(binary_to_list(BinList), ";", all),% devuelve lista donde cada elem es un nodo con sus atributos
            MapNodos = parsear_lista_nodos(List_nodos_separados),
            {ok, Socket, MapNodos};

        {error, Reason} ->
            {error, Reason}
    end.    

%Recibe la respuesta de la peticion enviada y maneja que hacer en cada caso.
% JobID(string), Job(string), CantRecursos(int), 
procesar_respuesta(JobID, Job, CantRecursos, Socket, Msg_RELEASE, JobTimeout, Pid_wait_jobs) ->
    case gen_tcp:recv(Socket, 0, JobTimeout) of %recibe la respuesta de C, TIMEOUT TODV No sabemos cuanto, pondriamos mas que C
        {error, timeout} -> %aca fue job timeout, recibiste un recurso(o no) pero esperaste mucho para otro(o para tu primer) entonces dio error timeout la fun tcp rcv
            borrarPendiente_and_registrarLog(JobID, Job, "POSIBLE DEADLOCK"),
            gen_tcp:send(Socket, <<Msg_RELEASE>>), %mandamos release devolviendo ese job
            pid_scheduler_job ! {JobID, Job, CantRecursos}; %lo mandamos d vuelta al buzon del receive para q desp intente d nuevo
            %Aca no mandamos ok al wait jobs pq todavia no termino este job
        {ok, Bin} -> %Si no dio error de timeout
            case binary_to_list(Bin) of
                "JOB_GRANTED " ++ _Rest -> %Si nos dieron los recursos, simulamos el job y devolvemos
                    borrarPendiente_and_registrarLog(JobID, Job, "JOB_GRANTED"),
                    io:format("Simulando trabajo. . .~n"),
                    timer:sleep(2000),
                    io:format("Trabajo finalizado!.~n"),
                    gen_tcp:send(Socket, <<Msg_RELEASE>>),%mandamos release devolviendo ese job
                    Pid_wait_jobs ! {ok}; %avisamos q el job termino

                "JOB_DENIED " ++ _Rest -> %Aca no nos dieron nada de recursos nos cancelaron de una, cancelamos el job(no hacemos nd)
                    borrarPendiente_and_registrarLog(JobID, Job, "JOB_DENIED"),
                    Pid_wait_jobs ! {ok}
            end
    end.

%Con esto, por cada job creado tenemos una conex en simultaneo hablando con C
handler_job(JobID, Job, CantRecursos, JobTimeout, Pid_wait_jobs, Puerto) ->
    case conectar_y_obtener_nodos(Puerto) of %SI la conexion fue exitosa:
        {ok, Socket, MapNodos} ->
            case armar_peticiones(JobID, Job, CantRecursos, MapNodos) of
                {error, no_alcanza} -> %Si no pudimos armar las peticiones no alcanzaron los nodos disponibles para la cantidad requerida de algun recurso
                    Pid_wait_jobs ! {ok},
                    gen_tcp:close(Socket);

                {Msg_REQUEST, Msg_RELEASE} -> %Si pudimos armarlas, las enviamos al agente e insertamos en la lista de pendientes el job
                    gen_tcp:send(Socket, <<Msg_REQUEST>>),
                    ets:insert(pendientes, {JobID, Job}),
                    procesar_respuesta(JobID, Job, CantRecursos, Socket, Msg_RELEASE, JobTimeout, Pid_wait_jobs),%Recibe la respuesta de la peticion enviada y maneja que hacer en cada caso.
                    gen_tcp:close(Socket)%Cerramos el socket
            end;
        {error, _Reason} -> %Error no pudimos conectarnos al socket
            Pid_wait_jobs ! {ok} %Enviamos que el job termino aunque fue con error.
    end.


wait_jobs(N) when N =< 0 ->
    cliente_pid ! fin; %Cuando terminan todos los jobs le mandamos msg avisando al cliente y ahora si puede finalizar.

%Funcion para esperar a que terminen todos los jobs.
wait_jobs(N) -> %N(int)
    receive 
        _ ->
            wait_jobs(N-1)
        end.

%Se conecta al socket, obtiene la lista con los nodos disponibles si funciona bien o exit si da error ya que no podemos hacer nada si no obtenemos los nodos disponibles.
% 
get_nodes_or_exit(Puerto)-> %packet, 2 lo q hace es q en los primeros 2 bytes pone la longitud y en lo qsigue el msg
    case  gen_tcp:connect("localhost", Puerto, [binary, {packet, 2}]) of %envio para conectarme al puerto 8100, si es exitosa devuelve ok socket
        {ok, Socket} ->
            gen_tcp:send(Socket, <<"GET_NODES\n">>), %consulto con el agente C respondera con una lista con los nodos disponoinbiles, necesito esto para armar listMax para generar los jobs
            % EJ: NODES 192.168.1.10:8100:cpu:4:mem:8192:gpu:1 ; 192.168.1.11:8101:cpu:2:mem:4096
            case gen_tcp:recv(Socket, 0) of
                {ok, BinList} -> 
                    gen_tcp:close(Socket),
                    {ok, BinList};
                {error, Reason} ->
                    gen_tcp:close(Socket),
                    exit({error_al_conectar, Reason}) %por mas q diga lista lor recibo como un binario q luego transformo a string
            end;
        {error, Reason} ->
            exit({error_al_conectar, Reason})
    end.

%Crea el proceso scheduler_jobs, si este muere captura el error y se encarga de volver a levantarlo.
%Recibe: JobTimeout(Timer en milisegundos), Pid_wait_jobs(Pid), Puerto(int)
%No retorna nada, vive siempre mientras el sistema este corriendo
supervisor_scheduler_jobs(JobTimeout, Pid_wait_jobs, Puerto) ->% JobTimeout(Timer en milisegundos), Pid_wait_jobs(Pid)
    process_flag(trap_exit, true), %hace que la señales de salida q provengan de procesos linkeades no maten automaticamente al proceso sino que se transf en msg que llegan al mailbox
    Pid_scheduler_job = spawn_link(?MODULE, scheduler_jobs, [JobTimeout, Pid_wait_jobs, Puerto]), %queda esperando jobs para enviar al sv en C
    register(pid_scheduler_job, Pid_scheduler_job), % lo registramos aca entonce ssi se cae lo volvemos a levantar y a registrar
    receive 
    {'EXIT', _From, _Reason} ->
        unregister(pid_scheduler_job),
        supervisor_scheduler_jobs(JobTimeout, Pid_wait_jobs, Puerto) %"Busca esta funcion en el modulo actual" entonces cuando volves a compilar la busca la nueva compilacion"
    end.

%Obtiene la lista de nodos activos, crea tabla de PENDIENTES, crea y LINKEA los procesos scheduler_job y wait_job 
%Recibe: N(cantidad de jobs a crear), Puerto(int)
%Retorna: ListMaximos(lista de 3 int, formada por la suma de esa cantidad entre todos los nodos disponibles)
inicializar_sistema(N, Puerto) -> 
    {ok, BinList} = get_nodes_or_exit(Puerto),
    List_nodos_separados = string:split(binary_to_list(BinList), ";", all),% devuelve lista donde cada elem es un nodo con sus atributos
    ListMaximos = obtener_cant_maxima_recursos(List_nodos_separados, [0,0,0]),
    JobTimeout = 5,
    %TABLA DE PENDIENTES: son los jobs q estan pendientes(fueron mandados y tdv no tienen rta), ets sierve para almacenar datos de forma compartida entre procesos
    ets:new(pendientes, [named_table, public, set]), %named table q la podemos llamar por su nombre, public cualq proceso puede acceder, set para q no repita
    Pid_wait_jobs = spawn_link(?MODULE, wait_jobs, [N]), %Creamos wait jobs para q cliente recien termine cuando terminen de ejecutarse todos los jobs y no teremine antes
    spawn_link(?MODULE, supervisor_scheduler_jobs, [JobTimeout, Pid_wait_jobs, Puerto]),%Si se cae el scheduler job lo levanta, spawnlink para q el server se entere si muere el supervisor
    ListMaximos.