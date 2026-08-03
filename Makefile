NAME		= webserv
CXX			= c++
CXXFLAGS	= -Wall -Wextra -Werror -std=c++98
INCLUDES	= -Iincludes -Iincludes/Utils -Iincludes/Http -Iincludes/Config -Iincludes/Cgi -Iincludes/Core
OBJ_DIR		= objs
SRCS		= sources/Utils/StringUtils.cpp \
			sources/Utils/NetUtils.cpp \
			sources/Utils/FsUtils.cpp \
			sources/Utils/HttpUtils.cpp \
			sources/Http/HttpRequest.cpp \
			sources/Http/HttpResponseBuilder.cpp \
			sources/Core/Client.cpp \
			sources/Core/Router.cpp \
			sources/Core/RequestHandler.cpp \
			sources/Config/ConfigParser.cpp \
			sources/Config/ConfigStructs.cpp \
			sources/Config/ListenTable.cpp \
			sources/Core/Server.cpp \
			sources/Cgi/ServerCgi.cpp \
			sources/main.cpp

OBJS		= $(SRCS:sources/%.cpp=$(OBJ_DIR)/%.o)

all: $(NAME)

$(NAME): $(OBJS)
	$(CXX) $(CXXFLAGS) $(OBJS) -o $(NAME)

$(OBJ_DIR)/%.o: sources/%.cpp
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) $(INCLUDES) -c $< -o $@

clean:
	rm -rf $(OBJ_DIR)

fclean: clean
	rm -f $(NAME)

re: fclean all

.PHONY: all clean fclean re
