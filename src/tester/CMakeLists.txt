set(TARGET_NAME ${PROJECT_NAME}-tester)

project(${TARGET_NAME})

find_package(LibMagic REQUIRED)
find_package(Threads REQUIRED)

include_directories(
	${CMAKE_CURRENT_BINARY_DIR}
	${CMAKE_CURRENT_SOURCE_DIR}
	${VSTSDK_INCLUDE_DIR}
)

add_definitions(-D__cdecl=)

set(SOURCES
	../common/dataport.cpp
	../common/event.cpp
	../common/filesystem.cpp
	../common/logger.cpp
	../common/storage.cpp
	../common/json.cpp
	../common/filesystem.cpp
	../common/moduleinfo.cpp
	main.cpp
)

add_executable(${TARGET_NAME} ${SOURCES} ${HEADERS})

target_link_libraries(${TARGET_NAME} ${LIBRARIES} ${LIBMAGIC_LIBRARY} ${CMAKE_THREAD_LIBS_INIT})

install(TARGETS ${TARGET_NAME} RUNTIME DESTINATION bin)
