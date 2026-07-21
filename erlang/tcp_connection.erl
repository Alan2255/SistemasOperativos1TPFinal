-module(tcp_connection).
-export([connect_agent/1, tcp_deliver/2]).

%=============================================== FUNCIONES TCP ===================================================

% Se conecta al agente C por TCP.
% Recibe: Puerto(int)
% Retorna: {ok, Socket} si pudo conectar, si falla, termina el proceso con exit({error_al_conectar, Reason}).
connect_agent(Puerto) ->
    case  gen_tcp:connect("localhost", Puerto, [binary, {packet, 2}, {active, false}]) of % envio para conectarme al puerto, si es exitosa devuelve ok socket.
        {ok, Socket} -> {ok, Socket};
        {error, Reason} -> exit({error_al_conectar, Reason})
    end.

% Loop que lee del socket TCP, cada mensaje recibido contiene un JobID, que se usa 
% para encontrar el proceso handler_job correspondiente en la ETS 'pendientes' y reenviarle el mensaje.
% De esta forma, tcp_deliver actúa como un despachador:
% recibe todos los mensajes del agente C y los distribuye al handler que está gestionando cada trabajo
% Se llama a si misma recursivamente después de procesar cada mensaje, para seguir escuchando.

% El mapa de nodos se pide una única vez, de forma síncrona, en system_init:request_map_nodes_initial, ANTES de que
% este proceso siquiera exista.

% Recibe: Socket, JobTimeout(int, no se usa acá pero se arrastra para futuros usos)
% No retorna nada relevante: corre indefinidamente hasta que el socket se cierra o falla.
tcp_deliver(Socket, JobTimeout) ->
    % Espera de forma bloqueante un mensaje desde el socket TCP.
    case gen_tcp:recv(Socket, 0) of
        {ok, Data} -> 
            io:format("~p~n", [Data]),
            Str = binary_to_list(Data),
            % Separa el mensaje por espacios para obtener el JobID.
            case string:tokens(Str, " ") of
                [_Comando, JobID | _Resto] ->
                    % Busca el JobID en la tabla ETS de trabajos pendientes.
                    case ets:lookup(pendientes, JobID) of
                        [{JobID, _Job, PidHandler, _Descuentos}] ->
                            % Si existe, le envía el mensaje al proceso handler_job encargado de ese JobID.
                            PidHandler ! {tcp_msg, Data};
                        [] ->
                            % Llegó una respuesta para un JobID que no tiene un proceso asociado.
                            io:format("[tcp_deliver] Alerta: Llego respuesta para JobID ~s sin proceso.~n", [JobID])
                    end;
                _ ->
                    % El mensaje recibido no tiene el formato esperado.
                    io:format("[tcp_deliver] Paquete invalido: ~p~n", [Str])
            end,
            % Vuelve a esperar el siguiente mensaje.
            tcp_deliver(Socket, JobTimeout);
        % El agente C cerró la conexión TCP
        {error, closed} -> 
            io:format("[--] Conexion con el servidor cerrada~n");
        % Ocurrió algún otro error en la comunicación.
        {error, Reason} -> 
            io:format("[tcp_deliver] Error: ~p~n", [Reason])
    end.
    