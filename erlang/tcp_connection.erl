-module(tcp_connection).
-export([connect_agent/1, send_map_nodes_request/1, tcp_deliver/2]).

%=============================================== FUNCIONES TCP ===================================================

% Devuelve el socket del agente
connect_agent(Puerto) ->
    case  gen_tcp:connect("localhost", Puerto, [binary, {packet, 2}, {active, false}]) of %envio para conectarme al puerto 8100, si es exitosa devuelve ok socket
        {ok, Socket} -> {ok, Socket};
        {error, Reason} -> exit({error_al_conectar, Reason})
    end.

% Devuelve un mapa de nodos, \\\\ NO LA USAMOS EN LA NUEVA IMPLEMENACION \\\\
send_map_nodes_request(Socket) -> 
    case gen_tcp:send(Socket, <<"GET_NODES">>) of 
        ok -> ok;
        {error, Reason} -> exit({error_send_get_map_nodes, Reason})
    end.

% Reparte los tcp que llegan desde el agente C a los procesos erlang
% ELIMINAMOS EL CASE CUANDO MANEJABA NODES PORQUE AHORA EL MAP NODOS SE PIDE UNA SOLA VEZ
% DESDE inicializar_sistema  antes de que este proceso exista.
tcp_deliver(Socket, JobTimeout) ->
    case gen_tcp:recv(Socket, 0) of
        {ok, Data} -> 
            io:format("[tcp_deliver] ~p~n", [Data]),
            Str = binary_to_list(Data),
            case string:tokens(Str, " ") of
                [_Comando, JobID | _Resto] ->
                    case ets:lookup(pendientes, JobID) of
                        [{JobID, _Job, PidHandler, _Descuentos}] ->
                            PidHandler ! {tcp_msg, Data};
                        [] ->
                            io:format("[tcp_deliver] Alerta: Llego respuesta para JobID ~s sin proceso.~n", [JobID])
                    end;
                _ ->
                    io:format("[tcp_deliver] Paquete invalido: ~p~n", [Str])
            end,
            tcp_deliver(Socket, JobTimeout);

        {error, closed} -> io:format("[tcp_deliver] Conexion cerrada.~n");
        {error, Reason} -> io:format("[tcp_deliver] Error: ~p~n", [Reason])
    end.
    