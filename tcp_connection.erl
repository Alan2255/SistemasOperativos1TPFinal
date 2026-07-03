-module(tcp_connection).
-export([connect_agent/1, get_map_nodes/1, tcp_deliver/2]).

%=============================================== FUNCIONES TCP ===================================================

% Devuelve el socket del agente
connect_agent(Puerto) ->
    case  gen_tcp:connect("localhost", Puerto, [binary, {packet, 2}, {active, false}]) of %envio para conectarme al puerto 8100, si es exitosa devuelve ok socket
        {ok, Socket} -> {ok, Socket};
        {error, Reason} -> exit({error_al_conectar, Reason})
    end.

% Devuelve un mapa de nodos
get_map_nodes(Socket) -> 
    case gen_tcp:send(Socket, <<"GET_NODES">>) of 
        ok -> ok;
        {error, Reason} -> exit({error_send_get_map_nodes, Reason})
    end.

% Reparte los tcp que llegan desde el agente C a los procesos erlang
tcp_deliver(Socket, JobTimeout) ->
    case gen_tcp:recv(Socket, 0) of
        {ok, Data} -> 
            io:format("[tcp_deliver] ~p~n", [Data]),
            Str = binary_to_list(Data),
            case Str of
                "NODES " ++ _Resto -> pid_scheduler_job ! {tcp_nodes, Data};
                _ ->
                    % Si no es un mensaje de NODES, asumimos que es un JOB_GRANTED/DENIED
                    % io:format("[tcp_deliver] {~p}~n", [Data]),
                    case string:tokens(Str, " ") of
                        [_Comando, JobID | _Resto] ->
                            case ets:lookup(pendientes, JobID) of
                                [{JobID, _Job, PidHandler}] ->
                                    PidHandler ! {tcp_msg, Data};
                                [] ->
                                    io:format("[tcp_deliver] Alerta: Llego respuesta para JobID ~s sin proceso.~n", [JobID])
                            end;
                        _ ->
                            io:format("[tcp_deliver] Paquete invalido: ~p~n", [Str])
                    end
            end,
            tcp_deliver(Socket, JobTimeout);

        {error, closed} -> io:format("[tcp_deliver] Conexion cerrada.~n");
        {error, Reason} -> io:format("[tcp_deliver] Error: ~p~n", [Reason])
    end.

    