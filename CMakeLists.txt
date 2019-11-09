# CMakeLists.txt for HttpNetworkTransport
#
# © 2018 by Richard Walters

cmake_minimum_required(VERSION 3.8)
set(This HttpNetworkTransport)

set(Headers
    include/HttpNetworkTransport/HttpClientNetworkTransport.hpp
    include/HttpNetworkTransport/HttpServerNetworkTransport.hpp
)

set(Sources
    src/HttpClientNetworkTransport.cpp
    src/HttpServerNetworkTransport.cpp
)

add_library(${This} STATIC ${Sources} ${Headers})
set_target_properties(${This} PROPERTIES
    FOLDER Libraries
)

target_include_directories(${This} PUBLIC include)

target_link_libraries(${This} PUBLIC
    Http
    StringExtensions
    SystemAbstractions
)

add_subdirectory(test)
