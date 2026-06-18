-module(main).
-export([]).
%nodos = host 
% Recibe la lista que envia el agente C con los nodos y recursos disponibles EJ: NODES 192.168.1.10:8100:cpu:4:mem:8192:gpu:1;192.168.1.11:8101:cpu:2:mem:4096
handler_list_nodos(List) ->
    List_nodos_separados = string:split(List, ";", all). % texto q queres partir, separador, todas las veces q aparezca
    %aca obtenemos una lista con cada nodo separada, EJ ["192.168.1.10:8100:cpu:4:mem:8192:gpu:1", "192.168.1.11:8101:cpu:2:mem:4096"]
    parsear_nodos(List_nodos_separados, [0,0,0]).  %esto devuelve una lista con los maximos, asi que la funcion retornara esta lista al ser la ultima expresion.
   

parsear_nodos([], ListMaximos) -> %cuando llega a la lista vacia, que retorne la lista con los valores maximos disponibles de CPU, MEM Y GPU
    ListMaximos.

%nodo primer elem de la lista, resto todo lo q sigue, la llamamos recursivamente hasta q llegue a que resto llame recursivmanete con [] list vacia, ahi retorna la lista con maximos
parsear_nodos([Nodo | Resto], ListMaximos) -> %cantidad d nodos, N para recorrer la lista desde 0 
    List_recursos = string:tokens(Nodo, ":"), %devuelve una lista con cda elem del nodo, ej [host, puerto, cpu, cntcpu, mem, cntmem, etc
    case List_recursos of 
        [Host, Puerto, "cpu", CantCPU,  "mem", CantMEM, "gpu", CantGPU] -> %mientras verifica, tmb asigna, es como hacer {todo esto} = List_recursos
                CPU = list_to_integer(CantCPU),
                MEM = list_to_integer(CantMEM),
                GPU = list_to_integer(CantGPU),
                %para q no crashee la primera iteracion comparara con 0 pq inicializamos la lista con 0,0,0
                MaxCPU = max(CPU, lists:nth(1, ListMaximos)),
                MaxMEM = max(MEM, lists:nth(2, ListMaximos)),
                MaxGPU = max(GPU, lists:nth(3, ListMaximos)),

                parsear_nodos(Resto, [MaxCPU, MaxMEM, MaxGPU]);
        _ -> %si no pudo asignar es pq esta mal el nodo
                io:format("Nodo mal formado!~n"),
                throw(badmatch)
        end.


%generar trabajo el cual necesitara tantos recursos, ==== dentro de ask_for_resources =====
% %JobID comienza de 0, N es la cantidad de jobs q pedimos generar, es recursiva
%Una vez genere el job, le mandara un msg a ask_for_resources con el IDjob y el job para que se comunique con el agente C para pedir recursos
generate_jobs(JobID, Pid, 0, ListMaximos, ListNodos) -> %% cuando N es 0, termina
        ok.


generate_jobs(JobID, Pid, N, ListMaximos, ListNodos) -> %TODOS LOS JOBS EL MISMO ORDEN
    [MaxCPU, MaxMEM, MaxGPU] = ListMaximos,
    List_recursos = [{"cpu", MaxCPU}, {"mem", MaxMEM}, {"gpu", MaxGPU}],
    Eleccion_recursos = rand:uniform(3), %random entre 1 y N (inclusive), elije cuantos recursos va a pedir

    case Eleccion_recursos of
        1 -> %significa q solo necesita un recurso 
            Nodo1= "@" ++ lists:nth(rand_uniform(length(ListNodos)), ListNodos)
            Num_recurso = rand:uniform(3), %como tienen los mismos indice listMaximos que Rcursos, si toca por ej 1, sabemos que es CPU.
            {Recurso, Maximo} = lists:nth(Num_recurso, List_recursos), %%devuelve la tupla con el recurso elegido y su cant maxima
            
            Cantidad = integer_to_list(rand:uniform(Maximo))

            Job = Recurso ++ ":" ++ Cantidad %esto crea el Job EJ : "recursorandom:numrandom"
            Pid ! {JobID, Job},

        2 ->     %ojo pq esta definido q es un recurso por nodo, si es el mismo recurso se reptie en el texto el host ej nodo1 cpu:2 nodo1:mem:3
            %como son 2 recursos, podr apedir maximo en 2 nodos diferentes, aunque podrias tocar el mismo, hacemos random
            Nodo1= "@" ++ lists:nth(rand_uniform(length(ListNodos)), ListNodos), %@ por el formato q pide el tp
            Nodo2= "@" ++ lists:nth(rand_uniform(length(ListNodos)), ListNodos),%puede ser igual q nodo1 y no pasa nada.
            Recurso_ignorado = lists:nth(rand:uniform(3), List_recursos_modificable),
            Recursos_elegidos = [Y || Y <- List_recursos, Y =/= Recurso_ignorado], %devuelve una lista sin el recurso ignorado

            {Recurso1, Maximo1} = lists:nth(1, Recursos_elegidos),
            {Recurso2, Maximo2} = lists:nth(2, Recursos_elegidos),

            Cantidad1 =  integer_to_list(rand_uniform(Maximo1)),%convertimos en string el entero random q salio 
            Cantidad2 =  integer_to_list(rand_uniform(Maximo2)),

            PeticionNodo1 = Nodo1 ++ ":" Recurso1 ++ ":" Cantidad1
            PeticionNodo2 = Nodo2 ++ ":" Recurso2 ++ ":" Cantidad2

            Job = PeticionNodo1 ++ " " ++ PeticionNodo2
            Pid ! {JobID, Job},
        3 ->
            Nodo1= "@" ++ lists:nth(rand_uniform(length(ListNodos)), ListNodos),% @ por el formato q pide el tp
            Nodo2= "@" ++ lists:nth(rand_uniform(length(ListNodos)), ListNodos),%puede ser igual q nodo1 y no pasa nada.
            Nodo3= "@" ++ lists:nth(rand_uniform(length(ListNodos)), ListNodos),

            {Recurso1, Maximo1} = lists:nth(1, List_recursos),
            {Recurso2, Maximo2} = lists:nth(2, List_recursos),
            {Recurso3, Maximo3} = lists:nth(2, List_recursos),

            Cantidad1 =  integer_to_list(rand_uniform(Maximo1)),%convertimos en string el entero random q salio 
            Cantidad2 =  integer_to_list(rand_uniform(Maximo2)),
            Cantidad3 =  integer_to_list(rand_uniform(Maximo3)),

            PeticionNodo1 = Nodo1 ++ ":" Recurso1 ++ ":" Cantidad1
            PeticionNodo2 = Nodo2 ++ ":" Recurso2 ++ ":" Cantidad2
            PeticionNodo3 = Nodo3 ++ ":" Recurso3 ++ ":" Cantidad3
            Job = PeticionNodo1 ++ " " ++ PeticionNodo2 ++ " " PeticionNodo3
            Pid ! {JobID, Job},

    end,

    generar(JobID+1, Pid, N-1, ListMaximos, ListNodos).

%Maneja la respuesta de cuando pedis recursos, ==== dentro de ask_for_resources =====
handler_answer() -> 

%pedir recursos para el trabajo, recibe la lista con los nodos disponibles
ask_for_resources(List, Socket) -> %$recibe pedidos de generate job y este se lo manda al server
    receive
        {JobID, Job} ->
            Msg = "JOB_REQUEST" ++ JobID ++ Job,
            gen_tcp:send(Socket, <<Msg>>), %envia el job pidiendo recursos a C, en binary
            
            {ok, Bin} = gen_tcp:recv(Socket, 0), %recibe la respuesta de C
            case binary_to_list(Bin) of
                "JOB_GRANTED " ++ Rest ->

                "JOB_DENIED " ++ Rest ->

                "JOB_TIMEOUT" ++ Rest ->


    ask_for_resources(List, Socket)
    
%liberar recursos
free_resources() ->

server(N) ->
    {ok, ListenSocket} = gen_tcp:listen(8100, [{reuseaddr, true}, {active, false}]),
    Pid_client = spawn_link(?MODULE, client, [N]),


    %packet, 2 lo q hace es q en los primeros 2 bytes pone la longitud y en lo qsigue el msg
client(N) ->
    {ok, Socket} = gen_tcp:connect("localhost", 8100, [binary, {packet, 2}]), %envio para conectarme al puerto 8100, si es exitosa devuelve ok socket
    gen_tcp:send(Socket, <<"GET_NODES\n">>), %consulto con el agente C sobre las lista de nodos q hay disponibles
    %me respondera con una lista de nodos vivos en formato de texto 
    % EJ: NODES 192.168.1.10:8100:cpu:4:mem:8192:gpu:1 ; 192.168.1.11:8101:cpu:2:mem:4096
    {ok, BinList} = gen_tcp:recv(Socket, 0),%por mas q diga lista lor recibo como un binario q luego transformo a string
    List = binary_to_list(BinList),
    Pid_ask_for_resources = spawn_link(?MODULE, ask_for_resources, [List,Socket]),%queda esperando jobs para enviar al sv en C
    ListMaximos = handler_list_nodos(List),%nos devuelve una lista con la cant max de CPU, MEM Y GPU
    ListNodos = % falta hacer la funcion para q devuelva los nodos disponibles
    generate_jobs(0, Pid_ask_for_resources, N, ListMaximos, ListNodos) %generara N jobs q se los enviara a ask_for_resources



    gen_tcp:close(Socket).


%consultar al agente C local sobre la lista de nodos participantes(IP, puesrto, recursos disponibles)
%C respondera con una lista de nodos vivos en formato texto por ejemeplo
% Ej: nodo recursos, nodo recursos
% en Erlang usamos esa lista para construir las solicitudes JOB_REQUEST
% (pq podemos elegir cualq nodo q tenga recursos q necesitemos, podemos pedir d varios a la vez)



