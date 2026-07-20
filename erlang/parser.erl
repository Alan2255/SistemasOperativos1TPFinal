-module(parser).
-export([eliminar_indice/2, parsear_lista_nodos/1, remover_prefijo_nodes/1, binList_to_MapNodos/1]).

%=============================================== FUNCIONES DE PARSEO ===================================================

%Split devuelve: primera lista con los primeros k elem y la segunda lista el resto ej lists:split(3, [a,b,c,d]) devolvera
% [a,b,c] [d], como queres borrar el elemento N, haces N-1 para q el elem q queres borrar quede al inicio de la segunda lista
% entonces haces {izq, [ _ | Der]} q ignora el primer elemento y desp unis toda la lista ignorando ese elem
% Recibe: N(int), Lista(Lista de 3 int)
% Retorna: La lista con el elemento eliminado
eliminar_indice(N, Lista) ->
    {Izq, [_ | Der]} = lists:split(N - 1, Lista), 
    Izq ++ Der.  
    
% string:tokens ":" , devuelve una lista con cda elem del nodo, ej [host, puerto, cpu, cntcpu, mem, cntmem, 
% Recibe: Nodo(string) -> ej: "192.168.1.2:8100:cpu:4:mem:8:gpu:2"
% Retorna: Una tupla {Host(string), Recursos(lista de 3 int)} -> ej: {"192.168.1.2:8100", [4,8,2]}
parsear_un_nodo(Nodo) ->%Nodo(string)
    [Host, Puerto, "cpu", CantCPU, "mem", CantMEM, "gpu", CantGPU] = string:tokens(Nodo, ":"),
    {Host ++ ":" ++ Puerto, [list_to_integer(CantCPU), list_to_integer(CantMEM), list_to_integer(CantGPU)]}.    

% Remueve el prefijo "NODES " si está presente en el string (si no está, devuelve el string sin cambios).
% Recibe: String(string) -> ej: "NODES 192.168.1.2:8100:cpu:4;..."
% Retorna: String(string) sin el prefijo -> ej: "192.168.1.2:8100:cpu:4;..."
parsear_lista_nodos(ListNodos) -> %ListNodos(list de strings)
    maps:from_list([parsear_un_nodo(Nodo) || Nodo <- ListNodos]).

% Remueve el prefijo "NODES " si está presente en el string
remover_prefijo_nodes("NODES " ++ Resto) -> 
    Resto;
remover_prefijo_nodes(String) ->
     String.

% Toma el binario crudo que llega del agente C con la lista de nodos, y devuelve directamente el mapa final listo para usar.
% Recibe: BinList(binary) -> ej: <<"NODES 192.168.1.2:8100:cpu:4:mem:8:gpu:2;192.168.1.3:8100:cpu:1:mem:2:gpu:0">>
% Retorna: Un mapa {Host => [CantCPU, CantMEM, CantGPU], ...}
binList_to_MapNodos(BinList) ->
    ListStr = binary_to_list(BinList),
    ListSinPrefijo = remover_prefijo_nodes(ListStr),
    List_nodos_separados = string:split(ListSinPrefijo, ";", all),
    parsear_lista_nodos(List_nodos_separados).