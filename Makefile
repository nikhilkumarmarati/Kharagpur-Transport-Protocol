CC = gcc
CFLAGS = -Wall -g
AR = ar
ARFLAGS = rcs

# Default target builds everything
all: libksocket.a user1 user2 initksocket

# Build the static library from ksocket.o
libksocket.a: ksocket.o
	$(AR) $(ARFLAGS) libksocket.a ksocket.o

# Compile ksocket.o from ksocket.c and ksocket.h
ksocket.o: ksocket.c ksocket.h
	$(CC) $(CFLAGS) -c ksocket.c

# Build user1 executable
user1: user1.o libksocket.a
	$(CC) $(CFLAGS) -o user1 user1.o -L. -lksocket

# Compile user1.o from user1.c (depends on ksocket.h)
user1.o: user1.c ksocket.h
	$(CC) $(CFLAGS) -c user1.c

# Build user2 executable
user2: user2.o libksocket.a
	$(CC) $(CFLAGS) -o user2 user2.o -L. -lksocket

# Compile user2.o from user2.c (depends on ksocket.h)
user2.o: user2.c ksocket.h
	$(CC) $(CFLAGS) -c user2.c

# Build initksocket executable
initksocket: initksocket.o libksocket.a
	$(CC) $(CFLAGS) -o initksocket initksocket.o -L. -lksocket

# Compile initksocket.o from initksocket.c
initksocket.o: initksocket.c ksocket.h
	$(CC) $(CFLAGS) -c initksocket.c

# Clean up all generated files
clean:
	rm -f *.o libksocket.a user1 user2 initksocket
