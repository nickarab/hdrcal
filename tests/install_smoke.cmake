# Verifica que `cmake --install` entrega um binário executável no bindir.
# Recebe os caminhos do CMakeLists; não depende do diretório de trabalho.

foreach(required_var HDRCAL_BUILD_DIR HDRCAL_INSTALL_PREFIX HDRCAL_INSTALL_BINDIR)
    if(NOT DEFINED ${required_var} OR "${${required_var}}" STREQUAL "")
        message(FATAL_ERROR "${required_var} não definido")
    endif()
endforeach()

set(prefix "${HDRCAL_INSTALL_PREFIX}")
file(REMOVE_RECURSE "${prefix}")

execute_process(
    COMMAND "${CMAKE_COMMAND}" --install "${HDRCAL_BUILD_DIR}" --prefix "${prefix}"
    RESULT_VARIABLE install_result
    OUTPUT_VARIABLE install_output
    ERROR_VARIABLE install_error
)
if(NOT install_result EQUAL 0)
    message(FATAL_ERROR
        "cmake --install falhou (${install_result})\n${install_output}\n${install_error}")
endif()

if(IS_ABSOLUTE "${HDRCAL_INSTALL_BINDIR}")
    set(binary "${HDRCAL_INSTALL_BINDIR}/hdrcal")
else()
    set(binary "${prefix}/${HDRCAL_INSTALL_BINDIR}/hdrcal")
endif()

if(NOT EXISTS "${binary}")
    message(FATAL_ERROR "binário instalado não encontrado em ${binary}")
endif()

execute_process(
    COMMAND "${binary}" --help
    RESULT_VARIABLE run_result
    OUTPUT_VARIABLE run_output
    ERROR_VARIABLE run_error
)
if(NOT run_result EQUAL 0)
    message(FATAL_ERROR
        "binário instalado não executou (${run_result})\n${run_output}\n${run_error}")
endif()
if(NOT run_output MATCHES "hdrcal")
    message(FATAL_ERROR "saída de --help não identifica o hdrcal:\n${run_output}")
endif()

message(STATUS "instalação verificada em ${binary}")
