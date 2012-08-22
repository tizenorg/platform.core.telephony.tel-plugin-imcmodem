TARGET	= nv_validation_test

SRCS	= main.c ../src/nvdata.c
OBJS	= $(SRCS:.c=.o)
PKGS	= tcore glib-2.0 gobject-2.0

CC		= gcc
CFLAGS	= -Wall -g `pkg-config --cflags $(PKGS)` -I../include
LIBS	= `pkg-config --libs $(PKGS)`

all: $(TARGET)

$(TARGET): $(OBJS)
	$(CC) $(OBJS) $(LIBS) -o $(TARGET)

.c.o: $(SRCS)
	$(CC) $(CFLAGS) -c $^ -o $@

clean:
	rm -f $(OBJS) $(TARGET)
