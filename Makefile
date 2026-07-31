NAME = webserv

SRC = main.cpp\
		Server.cpp\
		ConfigParser.cpp\
		ConfigStructs.cpp\
		cgi.cpp

CXX = c++

CXXFLAGS = -Wall -Wextra -Werror -std=c++98

OBJ = $(SRC:.cpp=.o)

all: $(NAME)

$(NAME): $(OBJ)
	$(CXX) $(CXXFLAGS) $(OBJ) -o $(NAME)

main.o: main.cpp configparser.hpp ConfigParser.hpp ConfigStructs.hpp Server.hpp
Server.o: Server.cpp Server.hpp configparser.hpp common.hpp
ConfigParser.o: ConfigParser.cpp ConfigParser.hpp ConfigStructs.hpp
ConfigStructs.o: ConfigStructs.cpp ConfigStructs.hpp
cgi.o: cgi.cpp cgi.hpp Server.hpp configparser.hpp common.hpp

clean:
	rm -f $(OBJ)

fclean: clean
	rm -rf $(NAME)

re: fclean all

.PHONY: all clean fclean re
