-module(job_manager).
-export([handler_job/8, recibir_jobs_y_armar_peticiones/4, armar_peticiones/3, wait_jobs/1]).

%=============================================== FUNCIONES SOBRE JOBS ===================================================
indice_recurso("cpu") -> 1;
indice_recurso("mem") -> 2;
indice_recurso("gpu") -> 3.
% El 2 en ets:update_counter significa modifica el 2ndo elemento de la tupla, basicamente q modifican el valor y no la clave
% ets:update_counter siempre suma, si queremos restar le pasamos un num negativo 

% Devuelve {Tomado, FaltaPedir} -> cuánto se tomó realmente, y cuánto queda pendiente.
% Recibe la ip del nodo, el indice(cpu,mem o gpu) y la cantidad que debe tomar
tomar_de_nodo(Host, Indice, Cantidad) ->
    Key = {Host, Indice},
    % Accede con la key al valor del recurso de ese nodo en la ets y resta la cantidad que desea tomar, la fun es atomica.
    NuevoValor = ets:update_counter(recursos_nodos, Key, {2, -Cantidad}),
    case NuevoValor >= 0 of
        % Si el resultado queda >= 0: el nodo tenía suficiente, tomamos Cantidad completa.
        true -> 
            {Cantidad, 0};  % alcanzó completo, no queda nada pendiente

        % Si el resultado queda < 0: el nodo no alcanzaba. Tomamos solo lo que tenía(el valor original), y reponemos el excedente que restamos de más para
        % dejar el contador en 0 (nunca negativo).
        false ->  
            % Ej: Recurso = 3, Cantidad(pedida) = 5 -> NuevoValor = -2.
            % Entonces lo que tomamos es 3, que es igual a Tomado = Cantidad + nuevoValor (Tomado = 5 + (-2)).
            Tomado = Cantidad + NuevoValor,
            % Repone el excedente para dejar el contador en 0 (no negativo).
            ets:update_counter(recursos_nodos, Key, {2, -NuevoValor}),
            % Retornamos cuanto se tomo realmente(para luego reponerlo) y cuanto falta pedir
            {Tomado, Cantidad - Tomado}
    end.

% Devuelve una Cantidad a un nodo (usado al revertir por JOB_DENIED/timeout).
devolver_a_nodo(Host, Indice, Cantidad) ->
    % Accede a la cantidad con la key {Host, Indice} y repone la cantidad que habiamos tomado. 2 significa que modifica 2ndo elem de la tupla(la cant)
    ets:update_counter(recursos_nodos, {Host, Indice}, {2, Cantidad}).

% Reparte Cantidad de un recurso entre la lista de nodos (en orden), tomando de a uno
% de forma atómica. El ultimo nodo de la lista recibe todo lo que quede pendiente,
% se le acepte o no (si no alcanza, tomar_de_nodo queda en 0 y devuelve lo real tomado).
% Devuelve {ListaPedidos, ListaDescuentosReales}:
% ListaPedidos: [{Host, CantidadPedida}, ...] -> para armar el mensaje al agente C
% ListaDescuentosReales: [{Host, Recurso, CantidadTomadaReal}, ...] -> para poder devolver despues

repartir_entre_nodos(_Indice, 0, _Nodos, _Recurso) -> 
    {[], []};

% Restante es lo q falta pedir.
repartir_entre_nodos(Indice, Restante, [Host], Recurso) ->
    % Ultimo nodo: se le pide TODO el restante, alcance o no.
    {Tomado, _Sobra} = tomar_de_nodo(Host, Indice, Restante),
    % Usamos Restante no tomado, pq el ultimo nodo pide todo lo q falta aunque no alcance, luego C haga job denied
    % La segunda lista si usa Tomado para poder ir teniendo en cuenta que reponer de la ets cuando recibamos job denied
    {[{Host, Restante}], [{Host, Recurso, Tomado}]};

repartir_entre_nodos(Indice, CantidadRestante, [Host | Resto], Recurso) -> 
    {Tomado, Sobra} = tomar_de_nodo(Host, Indice, CantidadRestante),
    case Tomado of
        0 -> 
            %% este nodo no tenía nada, seguimos con el resto sin agregarlo
            repartir_entre_nodos(Indice, CantidadRestante, Resto, Recurso);
        _ ->
            {ListaResto, DescuentosResto} = repartir_entre_nodos(Indice, Sobra, Resto, Recurso),
            % Va poniendo el resultado {Host, Tomado}  al principio de la lista que armaron los demas.
            % Van retornando las llamadas recursivas y se va formando la lista.
            {[{Host, Tomado} | ListaResto], [{Host, Recurso, Tomado} | DescuentosResto]}
    end.

% Reparte entre nodos no importa q no alcance luego ya seran denegados por C.
elegir_nodos(Recurso, Cantidad) -> 
    % Busca la lista con los nodos disponibles y su orden para saber cuando llega al ultimo nodo.
    [{orden_nodos, Nodos}] = ets:lookup(recursos_nodos, orden_nodos),
    Indice = indice_recurso(Recurso),
    repartir_entre_nodos(Indice, Cantidad, Nodos, Recurso).

% Descuentos es la lista de los descuentos que aplicamos a cada recurso
% Descuentos : [{Host, Recurso, CantidadTomadaReal}, ...]
handler_job(JobID, Job, CantRecursos, JobTimeout, Socket, Msg_REQUEST, Msg_RELEASE, Descuentos) ->
    ets:insert(pendientes, {JobID, Job, self(), Descuentos}), %Para evitar race cond insertamos primero y luego mandamos el msg
    gen_tcp:send(Socket, list_to_binary(Msg_REQUEST)),
    procesar_respuesta(JobID, Job, CantRecursos, Socket, Msg_RELEASE, JobTimeout, Descuentos).

% Devuelve a cada nodo lo que realmente se le habia tomado, de forma atomica.
% Recibe la lista con los descuentos aplicados 
revertir_descuentos(Descuentos) ->
    lists:foreach(fun({Host, Recurso, Cantidad}) ->
        Indice = indice_recurso(Recurso),
        devolver_a_nodo(Host, Indice, Cantidad)
    end,
     %lista sobre la que se aplica a cada elemento la funcion anonima fun.
     Descuentos
    ).

%Recibe la respuesta de la peticion del job enviado y maneja que hacer en cada caso, cuando termina un job, lo elimina de la tabla de Pendientes, registra su log y envia -
%- msg a wait_jobs avisando que termino.
% Recibe: JobID(string), Job(string), CantRecursos(int), Socket(int), Msg_Release(string), JobTimeuot(int en milisegundos), Pid_wait_jobs(Pid).
procesar_respuesta(JobID, Job, _CantRecursos, Socket, Msg_RELEASE, JobTimeout, Descuentos) ->
    receive 
        {tcp_msg, Bin} -> 
            case binary_to_list(Bin) of
                "JOB_GRANTED " ++ _Rest -> 
                    borrarPendiente_and_registrarLog(JobID, Job, "JOB_GRANTED"),
                    io:format("Simulando trabajo. . .~n"),
                    timer:sleep(2000),
                    io:format("Trabajo finalizado!.~n"),
                    gen_tcp:send(Socket, list_to_binary(Msg_RELEASE)),
                    %Devolvemos lo que habiamos descontado
                    revertir_descuentos(Descuentos),
                    pid_scheduler_job ! {job_terminado, JobID};
                    %Pid_wait_jobs ! {ok, Socket};

                "JOB_DENIED " ++ _Rest -> 
                    %Devolvemos lo que habiamos descontado
                    revertir_descuentos(Descuentos),
                    borrarPendiente_and_registrarLog(JobID, Job, "JOB_DENIED"),
                    pid_scheduler_job ! {job_terminado, JobID};
                    %Pid_wait_jobs ! {ok, Socket};
                
                Invalido ->
                    io:format("Formato de mensaje no esperado por el handler: ~p~n", [Invalido]),
                    %Devolvemos lo que habiamos descontado
                    revertir_descuentos(Descuentos),
                    pid_scheduler_job ! {job_terminado, JobID}
                    %Pid_wait_jobs ! {ok, Socket}
            end
    after JobTimeout -> 
        revertir_descuentos(Descuentos),
        borrarPendiente_and_registrarLog(JobID, Job, "POSIBLE DEADLOCK"),
        gen_tcp:send(Socket, list_to_binary(Msg_RELEASE)),
        pid_scheduler_job ! {job_terminado, JobID}
        % volver mandar al buzon
        % pid_scheduler_job ! {JobID, Job, CantRecursos}
    end.


% Cada vez q recibe un job arma la peticion y crea un proceso (conectado al mismo agente) para q mande y espere la rta del job
% se llama recursivamente para seguir atendiendo jobs
% Eliminamos MapNodos de los argumentos iniciales
recibir_jobs_y_armar_peticiones(Socket, JobTimeout, Pid_wait_jobs, JobsActivos) ->
    receive
         no_hay_mas_jobs -> 
            wait_jobs(JobsActivos);

         {job_terminado, _JobID} ->
            recibir_jobs_y_armar_peticiones(Socket, JobTimeout, Pid_wait_jobs, JobsActivos - 1);

         {JobID, Job, CantRecursos} -> 
            io:format("[scheduler] Procesando Job ~s (~s) ~n", [JobID, Job]),

            {Msg_REQUEST, Msg_RELEASE, Descuentos} = armar_peticiones(JobID, Job, CantRecursos),
            spawn(job_manager, handler_job, [JobID, Job, CantRecursos, JobTimeout, Socket, Msg_REQUEST, Msg_RELEASE, Descuentos]),
            recibir_jobs_y_armar_peticiones(Socket, JobTimeout, Pid_wait_jobs, JobsActivos + 1)
    end.

% Arma el string de pedido para un recurso, y devuelve también los descuentos reales aplicados.

armar_msg(Recurso, Cant) -> 
    % Lista de nodos con la cantidad del recurso pedido ej:[{"Nodo1",4},{"Nodo2",1}] y listDescuentos lista de descuentos aplicados
    {ListNodoCantidad, ListDescuentos} = elegir_nodos(Recurso, list_to_integer(Cant)),
    % A cada elem de la lista le aplica ese manejo de strings para construir el pedido de cada recurso a cada nodo, lo guarda como lista.
    ListPedidoPorNodo = [Nodo ++ ":" ++ Recurso ++ ":" ++ integer_to_list(Cantidad) || {Nodo, Cantidad} <- ListNodoCantidad],    
    % Une todos los elementos de la lista usando un espacio ej : ["Nodo1:cpu:4", "Nodo2:cpu:1"] -> "Nodo1:cpu:4 Nodo2:cpu:1"
    % Retorna eso y la lista con los descuentos aplicados.
    {string:join(ListPedidoPorNodo, " "), ListDescuentos}.

% Retornamos tambien Descuento en cada job armado para luego saber que devolver.
handler_msgs(JobID, Job, CantRecursos) ->
    % EJ : "cpu:10:mem:20" -> ["cpu","10","mem","20"]
    List_recursos = string:tokens(Job, ":"),
    case CantRecursos of 
        1 ->
            [Recurso1, Cant1] = List_recursos,
            {Msg, Descuentos1} = armar_msg(Recurso1, Cant1),
            {"JOB_REQUEST" ++ " " ++ JobID ++ " " ++ Msg, Descuentos1};

        2 ->
            [Recurso1, Cant1, Recurso2, Cant2] = List_recursos,
            {Msg1, Descuentos1} = armar_msg(Recurso1, Cant1),
            {Msg2, Descuentos2} = armar_msg(Recurso2, Cant2),
            {"JOB_REQUEST" ++ " " ++ JobID ++ " " ++ Msg1 ++ " " ++ Msg2, Descuentos1 ++ Descuentos2};

        3 ->
            [Recurso1, Cant1, Recurso2, Cant2, Recurso3, Cant3] = List_recursos,
            {Msg1, Descuentos1} = armar_msg(Recurso1, Cant1),
            {Msg2, Descuentos2} = armar_msg(Recurso2, Cant2),
            {Msg3, Descuentos3} = armar_msg(Recurso3, Cant3),
            {"JOB_REQUEST" ++ " " ++ JobID ++ " " ++ Msg1 ++ " " ++ Msg2 ++ " " ++ Msg3, Descuentos1 ++ Descuentos2 ++ Descuentos3}
    end.

% Ahora devuelve una TERNA {request, release, MapNodos actualizado}
armar_peticiones(JobID, Job, CantRecursos) ->
    %% _MapNodos ya no se usa (queda el parámetro para no romper la firma /4 exportada,
    %% pero ahora se lee todo directo de ETS). Si preferís, se puede sacar el parámetro.
    {Msg_REQUEST, Descuentos} = handler_msgs(JobID, Job, CantRecursos),
    Msg_RELEASE = "JOB_RELEASE" ++ " " ++ JobID,
    {Msg_REQUEST, Msg_RELEASE, Descuentos}.

%=============================================== WAIT / LOGS ===================================================
wait_jobs(0) ->
    io:format("[scheduler] Todos los jobs finalizaron.~n"),
    cliente_pid ! fin,
    ok;

wait_jobs(JobsActivos) ->
    receive
        {job_terminado, _JobID} ->
            wait_jobs(JobsActivos - 1)
    end.

% Si no existe el archivo lo crea y sino escribe al final
% Funcion que registra en un archivo log la fecha, hora, JobID , Job y respuesta del agente.
% Recibe: JobID(string), Job(string), Msg_Resultado(string)
registrar_log(JobID, Job, Msg_resultado)-> 
    {{Y,M,D}, {H,Mi,S}} = calendar:local_time(), %obtiene la fecha 
    Linea = io_lib:format("~p-~p-~p ~p:~p:~p | Job ~p | ~s | ~s~n", [Y, M, D, H, Mi, S, JobID, Job, Msg_resultado]), %Devuelve un string para usarlo, a dif de io:format que imprime directo en la consola
    file:write_file("../scheduler.log", Linea , [append]). %append para q no se borre lo anterior

%Borra el Job de la tabla de pendientes y registra su log.
%Recibe: %JobID(string), Job(string), Msg(String)
borrarPendiente_and_registrarLog(JobID, Job, Msg) -> 
    ets:delete(pendientes, JobID), %lo elimino de la lista de pendientes
    registrar_log(JobID, Job, Msg).


