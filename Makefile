NAME = webserv

SRC = main.cpp\
		Server.cpp\
		configparser.cpp\
		cgi.cpp

CXX = c++

CXXFLAGS = -Wall -Wextra -Werror -std=c++98

OBJ = $(SRC:.cpp=.o)

all: $(NAME)

$(NAME): $(OBJ)
	$(CXX) $(CXXFLAGS) $(OBJ) -o $(NAME)

main.o: main.cpp configparser.hpp Server.hpp common.hpp
Server.o: Server.cpp Server.hpp configparser.hpp common.hpp
configparser.o: configparser.cpp configparser.hpp
cgi.o: cgi.cpp Server.hpp configparser.hpp common.hpp

clean:
	rm -f $(OBJ)

fclean: clean
	rm -rf $(NAME)

re: fclean all

.PHONY: all clean fclean re
