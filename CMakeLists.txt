cmake_minimum_required(VERSION 2.8)
project(nanoping)

set(CMAKE_C_FLAGS "-O2 -Wall")

add_executable(nanoping
	hwtstamp_config.c
	nanoping.c
)
