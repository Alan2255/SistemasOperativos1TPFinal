-module(job_manager).
-export([handler_job/9, recibir_jobs_y_armar_peticiones/4, armar_peticiones/3, wait_jobs/1]).

%=============================================== FUNCIONES SOBRE JOBS ===================================================
indice_recurso("cpu") -> 1;
indice_recurso("mem") -> 2;
indice_recurso("gpu") -> 3.
% El 2 en ets:update_counter significa modifica el 2ndo elemento de la tupla, basicamente q modifican el valor y no la clave
% ets:update_counter siempre suma, si queremos restar le pasamos un num negativo 

% Devuelve {Tomado, FaltaPedir} -> cuánto se tomó realmente, y cuánto queda pendiente.
% Recibe la ip del nodo, el indice(cpu,mem o gpu) y la cantidad que debe tomar.
% Recibe: Host(string), Indice(int), Cantidad(int).
% Retorna: {Tomado(int), FaltaPedir(int)} 
% Tomado: cuánto se descontó realmente de la tabla ETS (puede ser igual a Cantidad si alcanzaba,
% o menor si el nodo no tenía suficiente.
% FaltaPedir: cuánto quedó sin poder tomarse (0 si alcanzó completo, un valor positivo si no alcanzó).
tomar_de_nodo(Host, Indice, Cantidad) ->
    Key = {Host, Indice},
    % Accede con la key al valor del recurso de ese nodo en la ets y resta la cantidad que desea tomar, la fun es atómica.
    NuevoValor = ets:update_counter(recursos_nodos, Key, {2, -Cantidad}),
    case NuevoValor >= 0 of
        % Si el resultado queda >= 0: el nodo tenía suficiente, tomamos Cantidad completa.
        true -> 
            {Cantidad, 0};  % Alcanzó completo, no queda nada pendiente

        % Si el resultado queda < 0: el nodo no alcanzaba. Tomamos solo lo que tenía(el valor original), y reponemos 
        % el excedente que restamos de más para dejar el contador en 0 (nunca negativo).
        false ->  
            % Ej: Recurso = 3, Cantidad(pedida) = 5 -> NuevoValor = -2.
            % Entonces lo que tomamos es 3, que es igual a Tomado = Cantidad + nuevoValor (Tomado = 5 + (-2)).
            Tomado = Cantidad + NuevoValor,
            % Repone el excedente para dejar el contador en 0 (no negativo).
            ets:update_counter(recursos_nodos, Key, {2, -NuevoValor}),
            % Retornamos cuanto se tomo realmente (para luego reponerlo) y cuanto falta pedir
            {Tomado, Cantidad - Tomado}
    end.

% Función que accede a la cantidad del host con la key {Host, Indice} y repone la cantidad que había sido tomada.
% Devuelve una Cantidad a un nodo (usado al revertir por JOB_DENIED/timeout).
% Recibe: Host(string), Indice(int), Cantidad(int)
devolver_a_nodo(Host, Indice, Cantidad) ->
    ets:update_counter(recursos_nodos, {Host, Indice}, {2, Cantidad}). % 2 significa que modifica 2ndo elem de la tupla(la cant)

% Reparte Cantidad de un recurso entre la lista de nodos (en orden), tomando de a uno
% de forma atómica(via tomar_de_nodo). El ultimo nodo de la lista se le PIDE todo lo que quede pendiente,
% se le acepte o no (si no alcanza, tomar_de_nodo queda en 0 y devuelve lo real tomado).

% Recibe: Indice(int), Restante(int), Nodos(List de Hosts), Recurso(string).
% Retorna: {ListPedidos, ListDescuentos}
% ListaPedidos: [{Host(string), CantidadPedida(int)}, ...] -> para armar el mensaje al agente C.
% ListaDescuentos: [{Host(string), Recurso(string), CantidadTomadaReal(int)} ... ] -> para poder devolver despues.

% Caso base: ya no queda nada por repartir (se completó el pedido con nodos anteriores).
% Corta la recursión sin agregar ningún nodo más a ninguna de las dos listas.
repartir_entre_nodos(_Indice, 0, _Nodos, _Recurso) -> 
    {[], []};

% Caso último nodo: queda un solo nodo en la lista y todavía falta repartir Restante.
% A este nodo se le pide TODO lo que falta (Restante), tenga o no esa cantidad disponible.
repartir_entre_nodos(Indice, Restante, [Host], Recurso) ->
    % Tomado (lo que tomó de verdad), Sobra (lo que quedo sin poder tomarse)
    {Tomado, _Sobra} = tomar_de_nodo(Host, Indice, Restante),
    % Usamos Restante no tomado, pq el ultimo nodo pide todo lo q falta aunque no alcance, luego C hara JOB_DENIED.
    % La segunda lista si usa Tomado (lo que realmente se reto de la tabla) para tener en cuenta cuanto reponer de la ets cuando recibamos JOB_DENIED.
    {[{Host, Restante}], [{Host, Recurso, Tomado}]};

% Caso general: quedan 2 o más nodos y todavía falta repartir CantidadRestante.
% A este nodo se le toma como máximo lo que tiene disponible (tomar_de_nodo se encarga
% de no pasarse), y lo que no se pudo tomar acá (Sobra) se sigue repartiendo entre el resto.
repartir_entre_nodos(Indice, CantidadRestante, [Host | Resto], Recurso) -> 
    % Tomado (lo que tomó de verdad), Sobra (lo que quedo sin poder tomarse).
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

% Dado un Recurso ("cpu","mem" o "gpu") y una Cantidad a pedir, recorre los nodos (en el orden guardado en orden_nodos) 
% y reparte esa Cantidad entre ellos, descontando de la tabla ETS a medida que toma.
% El último nodo recibe todo lo que quede pendiente, alcance o no.

% Recibe: Recurso(string), Cantidad(int).
% Retorna: {ListPedidos, ListDescuentos}
% ListaPedidos: [{Host(string), CantidadPedida(int)}, ...] -> para armar el mensaje al agente C,
% ListaDescuentos: [{Host(string), Recurso(string), CantidadTomadaReal(int)} ... ] -> para poder devolver despues.
elegir_nodos(Recurso, Cantidad) -> 
    % Busca la lista con los nodos disponibles y su orden para saber cuando llega al ultimo nodo.
    [{orden_nodos, Nodos}] = ets:lookup(recursos_nodos, orden_nodos),
    Indice = indice_recurso(Recurso),
    repartir_entre_nodos(Indice, Cantidad, Nodos, Recurso).

% Maneja el ciclo de vida completo de UN job ya armado.
% Guarda el job en 'pendientes' (junto con quién lo maneja y qué se le descontó a cada nodo, para poder revertir si hace falta),
% manda el JOB_REQUEST al agente C por TCP, y queda esperando su respuesta (delegado en procesar_respuesta).
% Se spawnea SIN LINK: si este proceso muere, no afecta al resto del sistema ya que cada job es independiente.

% Descuentos es la lista de los descuentos que aplicamos a cada recurso ej: [{Host, Recurso, CantidadTomadaReal}, ...]
% Recibe: JobID(string), Job(string), CantRecursos(int), JobTimeout(int), Socket,
% Msg_REQUEST(string), Msg_RELEASE(string), Descuentos(List de tuplas),TimeJob(int).
handler_job(JobID, Job, CantRecursos, JobTimeout, Socket, Msg_REQUEST, Msg_RELEASE, Descuentos, TimeJob) ->
    ets:insert(pendientes, {JobID, Job, self(), Descuentos}), %Para evitar race cond insertamos primero y luego mandamos el msg
    gen_tcp:send(Socket, list_to_binary(Msg_REQUEST)),
    procesar_respuesta(JobID, Job, CantRecursos, Socket, Msg_RELEASE, JobTimeout, Descuentos, TimeJob).

% Devuelve a cada nodo lo que realmente se le había tomado, de forma atómica.
% Recibe: Descuentos(List de tuplas).
revertir_descuentos(Descuentos) ->
    lists:foreach(fun({Host, Recurso, Cantidad}) ->
        Indice = indice_recurso(Recurso),
        devolver_a_nodo(Host, Indice, Cantidad)
    end,
     % A cada elemento de esta lista se le aplica la funcion anonima(fun) definida arriba.
     Descuentos
    ).

% Recibe la respuesta de la peticion del job enviado y maneja que hacer en cada caso, cuando termina un job, lo elimina de la tabla de Pendientes, registra su log y envia -
%- msg a wait_jobs avisando que termino.
% Recibe: JobID(string), Job(string), CantRecursos(int), Socket, Msg_Release(string), JobTimeuot(int), Descuentos(list tuplas), TimeJob(int).
procesar_respuesta(JobID, Job, _CantRecursos, Socket, Msg_RELEASE, JobTimeout, Descuentos, TimeJob) ->
    receive 
        {tcp_msg, Bin} -> 
            case binary_to_list(Bin) of
                "JOB_GRANTED " ++ _Rest -> 
                    borrarPendiente_and_registrarLog(JobID, Job, "JOB_GRANTED"),
                    io:format("Simulando trabajo. . .~n"),
                    timer:sleep(1000 * TimeJob),
                    io:format("Trabajo finalizado!~n"),
                    gen_tcp:send(Socket, list_to_binary(Msg_RELEASE)),
                    % Devolvemos lo que habíamos descontado
                    revertir_descuentos(Descuentos),
                    pid_scheduler_job ! {job_terminado, JobID};
                   

                "JOB_DENIED " ++ _Rest -> 
                    %Devolvemos lo que habíamos descontado
                    revertir_descuentos(Descuentos),
                    borrarPendiente_and_registrarLog(JobID, Job, "JOB_DENIED"),
                    pid_scheduler_job ! {job_terminado, JobID};
                    
                
                Invalido ->
                    io:format("Formato de mensaje no esperado por el handler: ~p~n", [Invalido]),
                    %Devolvemos lo que habíamos descontado
                    revertir_descuentos(Descuentos),
                    pid_scheduler_job ! {job_terminado, JobID}
                
            end
    after JobTimeout -> 
        revertir_descuentos(Descuentos),
        borrarPendiente_and_registrarLog(JobID, Job, "POSIBLE DEADLOCK"),
        gen_tcp:send(Socket, list_to_binary(Msg_RELEASE)),
        pid_scheduler_job ! {job_terminado, JobID}
        % volver mandar al buzon
        % pid_scheduler_job ! {JobID, Job, CantRecursos}
    end.

% Esta funcion es llamada por scheduler_jobs, puede recibir 3 mensajes diferentes:  
% - no_hay_mas_jobs enviado por generate_jobs cuando en modo random ya termino de generar todos los jobs, solo queda esperar a que terminen,
%   llama a la funcion wait_jobs para que espere a que terminen todos los jobs activos.
% - {job_terminado, _JobID} enviador por el proceso handler_job en la llamada de la funcion procesar_respuesta, significa que un job terminó,
%   llama a la funcion recursivamente restando en 1 los jobs activos.
% - {JobID(int), Job(string), CantRecursos(int)} enviado por generate_jobs, en este caso arma las peticiones,
%   crea un proceso (conectado al mismo agente) que atienda el job (handler_job) y vuelve a llamarse recurisvamente, incrementando en 1 los jobs activos.

% Recibe : Socket, JobTimeout(int), JobActivos(int), TimeJob(int).
recibir_jobs_y_armar_peticiones(Socket, JobTimeout, JobsActivos, TimeJob) ->
    receive
         no_hay_mas_jobs -> 
            wait_jobs(JobsActivos);

         {job_terminado, _JobID} ->
            recibir_jobs_y_armar_peticiones(Socket, JobTimeout, JobsActivos - 1, TimeJob);

         {JobID, Job, CantRecursos} -> 
            {Msg_REQUEST, Msg_RELEASE, Descuentos} = armar_peticiones(JobID, Job, CantRecursos),
            spawn(job_manager, handler_job, [JobID, Job, CantRecursos, JobTimeout, Socket, Msg_REQUEST, Msg_RELEASE, Descuentos, TimeJob]),
            recibir_jobs_y_armar_peticiones(Socket, JobTimeout, JobsActivos + 1, TimeJob)
    end.

% Recibe el nombre del recurso, "cpu", "mem", o "gpu", la cantidad de este, arma el string con el PEDIDO 
% de un recurso a distintos(o unico) nodo y devuelve también los descuentos  reales aplicados.
% Recibe: Recurso(string), Cant(string).
% Retorna : {String, Lista de tuplas con la forma: {Host(string), Recurso(string), CantidadTomadaReal(int)} }.
armar_msg(Recurso, Cant) -> 
    % Lista de nodos con la cantidad del recurso pedido ej:[{"Nodo1",4},{"Nodo2",1}] y listDescuentos lista de descuentos aplicados
    {ListNodoCantidad, ListDescuentos} = elegir_nodos(Recurso, list_to_integer(Cant)),
    % A cada elem de la lista le aplica ese manejo de strings para construir el pedido de cada recurso a cada nodo, lo guarda como lista.
    ListPedidoPorNodo = [Nodo ++ ":" ++ Recurso ++ ":" ++ integer_to_list(Cantidad) || {Nodo, Cantidad} <- ListNodoCantidad],    
    % Une todos los elementos de la lista usando un espacio ej : ["Nodo1:cpu:4", "Nodo2:cpu:1"] -> "Nodo1:cpu:4 Nodo2:cpu:1"
    % Retorna eso y la lista con los descuentos aplicados.
    {string:join(ListPedidoPorNodo, " "), ListDescuentos}.

% Funcion que recibe jobs con su informacion por parametro y crea el mensaje JOB_REQUEST.
% Recibe: JobID(string), Job(String), CantRecursos(int).
% Retorna : Msg_REQUEST(string), Descuentos(Lista de tuplas con la forma: {Host(string), Recurso(string), CantidadTomadaReal(int)} }.
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

% Arma las peticiones JOB_REQUEST y JOB_RELEASE, guardando los descuentos aplicados a cada recurso para luego reponerlos.
% Recibe: JobID(int), Job(String), CantRecursos(int).
% Retorna : {Msg_REQUEST(string), Msg_RELEASE(string), Descuentos(Lista de tuplas con la forma: {Host(string), Recurso(string), CantidadTomadaReal(int)}})
armar_peticiones(JobID, Job, CantRecursos) ->
    {Msg_REQUEST, Descuentos} = handler_msgs(JobID, Job, CantRecursos),
    Msg_RELEASE = "JOB_RELEASE" ++ " " ++ JobID,
    {Msg_REQUEST, Msg_RELEASE, Descuentos}.

%=============================================== WAIT / LOGS ===================================================

% Cuando ya no quedan jobs activos (JobsActivos llegó a 0), avisa a cliente_pid que todo terminó.
wait_jobs(0) ->
    io:format("Todos los jobs finalizaron~n"),
    cliente_pid ! fin,
    ok;

% Espera a que terminen todos los jobs que estan activos, recibe un mensaje 
% avisando que termino un job y llama a la funcion recursivamente restando 
% en 1 a JobsActivos.
% Recibe: JobsActivos(int)
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
%Recibe: JobID(string), Job(string), Msg(String)
borrarPendiente_and_registrarLog(JobID, Job, Msg) -> 
    ets:delete(pendientes, JobID), %lo elimino de la lista de pendientes
    registrar_log(JobID, Job, Msg).


