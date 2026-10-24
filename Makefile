## you have command.cc command.h tokenizer.cc tokenizer.h you need compile them and link
## them to one object file called  : "myshell" 
## so we should run make , that will create our object myshell ,
## ./myshell should run your application 

CXX = g++
CXXFLAGS = -Wall -Wextra -g

TARGET = myshell

SRCS = command.cc tokenizer.cc
OBJS = $(SRCS:.cc=.o)

all: $(TARGET)

$(TARGET): $(OBJS)
	$(CXX) $(CXXFLAGS) -o $(TARGET) $(OBJS)

command.o: command.cc command.h tokenizer.h
	$(CXX) $(CXXFLAGS) -c command.cc

tokenizer.o: tokenizer.cc tokenizer.h
	$(CXX) $(CXXFLAGS) -c tokenizer.cc

clean:
	rm -f $(OBJS) $(TARGET)