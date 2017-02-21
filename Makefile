

СXX		= g++
CFLAGS		= -c -pipe -std=c++14 -g -Wall -W -I.
LIBS		=  $(SUBLIBS)	-lboost_system -lpthread -lboost_coroutine

TARGET		= http_client
OBJECTS		= main.o \
	 	  connects.o

all : $(TARGET)
	
$(TARGET) : $(OBJECTS)
	$(CXX) -o $(TARGET) $(OBJECTS) $(LIBS)

main.o : main.cpp ./connects.hpp
	$(CXX) $(CFLAGS)  main.cpp 

connects.o : connects.cpp ./connects.hpp
	$(CXX) $(CFLAGS)  connects.cpp

clean:
	rm -rf *.o $(TARGET)
