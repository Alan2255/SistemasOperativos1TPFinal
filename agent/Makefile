CC = gcc
CFLAGS = -Wall -Wextra

agent:
	gcc agent.c functions/*.c -o agent

tokenize_str_test:
	$(CC) $(CFLAGS) -o tokenize_str_test \
	    functions/tokenize_str.c \
	    functions/tokenize_str_test.c

tokenize_str_run_and_clean: tokenize_str_test
	./tokenize_str_test && rm -f tokenize_str_test

parse_announce_test:
	$(CC) $(CFLAGS) -o parse_announce_test \
	    functions/parse_announce.c \
	    functions/parse_announce_test.c functions/tokenize_str.c

parse_announce_test_run_and_clean: parse_announce_test
	./parse_announce_test && rm -f parse_announce_test

fdinfo_test:
	$(CC) $(CFLAGS) -o fdinfo_test functions/fdinfo.c \
	    functions/fdinfo_test.c

fdinfo_test_run_and_clean: fdinfo_test
	./fdinfo_test && rm -f fdinfo_test