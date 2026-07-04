-module(parser).
-export([eliminar_indice/2, parsear_lista_nodos/1, remover_prefijo_nodes/1]).

%=============================================== FUNCIONES DE PARSEO ===================================================

%Split devuelve: primera lista con los primeros k elem y la segunda lista el resto ej lists:split(3, [a,b,c,d]) devolvera
% [a,b,c] [d], como queres borrar el elemento N, haces N-1 para q el elem q queres borrar quede al inicio de la segunda lista
% entonces haces {izq, [ _ | Der]} q ignora el primer elemento y desp unis toda la lista ignorando ese elem
% Recibe: N(int), Lista(Lista de 3 int)
% Retorna: La lista con el elemento eliminado
eliminar_indice(N, Lista) ->
    {Izq, [_ | Der]} = lists:split(N - 1, Lista), 
    Izq ++ Der.  
    
% string:toekns ":" , devuelve una lista con cda elem del nodo, ej [host, puerto, cpu, cntcpu, mem, cntmem, 
% Recibe: Nodo(string)
% Retorna: Una tupla de la forma {Host, [CantCPU, CantMem, CantGPU}

parsear_un_nodo(Nodo) ->%Nodo(string)
    [Host, Puerto, "cpu", CantCPU, "mem", CantMEM, "gpu", CantGPU] = string:tokens(Nodo, ":"),
    {Host ++ ":" ++ Puerto, [list_to_integer(CantCPU), list_to_integer(CantMEM), list_to_integer(CantGPU)]}.    

%A cada nodo que es un string lo transforma en una tupla de la forma {Host, [CantCPU, CantMem, CantGPU}, de esta forma arma una lista con list comprehension
% y finalmente transforma la lista en un mapa
% Recibe: ListNodos(lista de strings)
% Retorna: Un mapa de la forma {Nodo1 => [cantCPU, cantMEM, cantGPU], Nodo2 => [cantCPU, cantMEM, cantGPU], etc}
parsear_lista_nodos(ListNodos) -> %ListNodos(list de strings)
    maps:from_list([parsear_un_nodo(Nodo) || Nodo <- ListNodos]).

% Remueve el prefijo "NODES " si está presente en el string
remover_prefijo_nodes("NODES " ++ Resto) -> 
    Resto;
remover_prefijo_nodes(String) ->
     String.


