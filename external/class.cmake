option(ENABLE_CLASS "Enable CLASS support (as a submodule)." ON)

if(ENABLE_CLASS)

  # initialize the class submodule if necessary
  if(EXISTS "${CMAKE_CURRENT_LIST_DIR}/class/Makefile")
    message(STATUS "class submodule is initialized.")
  else()
    message(STATUS "class submodule is NOT initialized: executing git command")
    execute_process(COMMAND git submodule update --init -- external/class
      WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR})
  endif()

  # include directories
  set(CLASS_INCLUDE_DIR ${CMAKE_CURRENT_LIST_DIR}/class/include)
  set(CLASS_INCLUDE_CPP_DIR ${CMAKE_CURRENT_LIST_DIR}/class/cpp)

  # build method for class
  option(USE_CLASS_MAKEFILE "Build CLASS using its own Makefile." OFF)

  if(USE_CLASS_MAKEFILE)
    # list of object files generated by class
    set(CLASS_OBJECT_FILES
      ${CMAKE_CURRENT_LIST_DIR}/class/build/arrays.o
      ${CMAKE_CURRENT_LIST_DIR}/class/build/background.o
      ${CMAKE_CURRENT_LIST_DIR}/class/build/common.o
      ${CMAKE_CURRENT_LIST_DIR}/class/build/dei_rkck.o
      ${CMAKE_CURRENT_LIST_DIR}/class/build/evolver_ndf15.o
      ${CMAKE_CURRENT_LIST_DIR}/class/build/evolver_rkck.o
      ${CMAKE_CURRENT_LIST_DIR}/class/build/growTable.o
      ${CMAKE_CURRENT_LIST_DIR}/class/build/helium.o
      ${CMAKE_CURRENT_LIST_DIR}/class/build/history.o
      ${CMAKE_CURRENT_LIST_DIR}/class/build/hydrogen.o
      ${CMAKE_CURRENT_LIST_DIR}/class/build/hyperspherical.o
      ${CMAKE_CURRENT_LIST_DIR}/class/build/hyrectools.o
      ${CMAKE_CURRENT_LIST_DIR}/class/build/input.o
      ${CMAKE_CURRENT_LIST_DIR}/class/build/lensing.o
      ${CMAKE_CURRENT_LIST_DIR}/class/build/nonlinear.o
      ${CMAKE_CURRENT_LIST_DIR}/class/build/output.o
      ${CMAKE_CURRENT_LIST_DIR}/class/build/parser.o
      ${CMAKE_CURRENT_LIST_DIR}/class/build/perturbations.o
      ${CMAKE_CURRENT_LIST_DIR}/class/build/primordial.o
      ${CMAKE_CURRENT_LIST_DIR}/class/build/quadrature.o
      ${CMAKE_CURRENT_LIST_DIR}/class/build/sparse.o
      ${CMAKE_CURRENT_LIST_DIR}/class/build/spectra.o
      ${CMAKE_CURRENT_LIST_DIR}/class/build/thermodynamics.o
      ${CMAKE_CURRENT_LIST_DIR}/class/build/transfer.o
    )

    # python3
    find_package(Python3 REQUIRED COMPONENTS Interpreter)

    # command to build class using its own makefile
    add_custom_command(OUTPUT ${CLASS_OBJECT_FILES}
      COMMAND PYTHON=${Python3_EXECUTABLE} CC=${CMAKE_C_COMPILER} make
      WORKING_DIRECTORY ${CMAKE_CURRENT_LIST_DIR}/class
    )

    # target for class objects
    add_custom_target(class_objects DEPENDS ${CLASS_OBJECT_FILES})

    # library for class cpp wrappers
    add_library(class_cpp 
      ${CMAKE_CURRENT_LIST_DIR}/class/cpp/Engine.cc
      ${CMAKE_CURRENT_LIST_DIR}/class/cpp/ClassEngine.cc)
    target_include_directories(class_cpp
      PRIVATE ${CMAKE_CURRENT_LIST_DIR}/class/include)
  else(USE_CLASS_MAKEFILE)

    # list of CLASS source files
    set(CLASS_SOURCE_FILES
      ${CMAKE_CURRENT_LIST_DIR}/class/tools/growTable.c
      ${CMAKE_CURRENT_LIST_DIR}/class/tools/dei_rkck.c
      ${CMAKE_CURRENT_LIST_DIR}/class/tools/sparse.c
      ${CMAKE_CURRENT_LIST_DIR}/class/tools/evolver_rkck.c
      ${CMAKE_CURRENT_LIST_DIR}/class/tools/evolver_ndf15.c
      ${CMAKE_CURRENT_LIST_DIR}/class/tools/arrays.c
      ${CMAKE_CURRENT_LIST_DIR}/class/tools/parser.c
      ${CMAKE_CURRENT_LIST_DIR}/class/tools/quadrature.c
      ${CMAKE_CURRENT_LIST_DIR}/class/tools/hyperspherical.c
      ${CMAKE_CURRENT_LIST_DIR}/class/tools/trigonometric_integrals.c
      ${CMAKE_CURRENT_LIST_DIR}/class/tools/common.c
      ${CMAKE_CURRENT_LIST_DIR}/class/source/input.c
      ${CMAKE_CURRENT_LIST_DIR}/class/source/background.c
      ${CMAKE_CURRENT_LIST_DIR}/class/source/thermodynamics.c
      ${CMAKE_CURRENT_LIST_DIR}/class/source/perturbations.c
      ${CMAKE_CURRENT_LIST_DIR}/class/source/primordial.c
      ${CMAKE_CURRENT_LIST_DIR}/class/source/nonlinear.c
      ${CMAKE_CURRENT_LIST_DIR}/class/source/transfer.c
      ${CMAKE_CURRENT_LIST_DIR}/class/source/spectra.c
      ${CMAKE_CURRENT_LIST_DIR}/class/source/lensing.c
      ${CMAKE_CURRENT_LIST_DIR}/class/hyrec/hyrectools.c
      ${CMAKE_CURRENT_LIST_DIR}/class/hyrec/helium.c
      ${CMAKE_CURRENT_LIST_DIR}/class/hyrec/hydrogen.c
      ${CMAKE_CURRENT_LIST_DIR}/class/hyrec/history.c
      ${CMAKE_CURRENT_LIST_DIR}/class/source/output.c
      ${CMAKE_CURRENT_LIST_DIR}/class/cpp/Engine.cc
      ${CMAKE_CURRENT_LIST_DIR}/class/cpp/ClassEngine.cc
    )
    
    # create the library
    add_library(class ${CLASS_SOURCE_FILES})
    target_include_directories(class PRIVATE ${CLASS_INCLUDE_DIR})
    target_include_directories(class PRIVATE ${CLASS_INCLUDE_CPP_DIR})
    target_compile_options(class PRIVATE "-ffast-math")
    target_compile_options(class PRIVATE "-D__CLASSDIR__=\"${CMAKE_CURRENT_LIST_DIR}/class\"")
    set_property(TARGET class PROPERTY POSITION_INDEPENDENT_CODE ON)
    target_include_directories(class PRIVATE ${CMAKE_CURRENT_LIST_DIR}/class/hyrec)
    target_compile_options(class PRIVATE "-DHYREC")
    set_target_properties(class PROPERTIES CXX_STANDARD 14 C_STANDARD 11)
    # target_compile_options(class PRIVATE "-Wall")
    # target_compile_options(class PRIVATE "-Wextra")
    # target_compile_options(class PRIVATE "-pedantic")
  endif(USE_CLASS_MAKEFILE)
endif(ENABLE_CLASS)

# macro to setup include dir and link libraries for target using class
macro(target_setup_class target_name)
  if(ENABLE_CLASS)
    target_include_directories(${target_name}
      PRIVATE ${CLASS_INCLUDE_DIR})
    target_include_directories(${target_name}
      PRIVATE ${CLASS_INCLUDE_CPP_DIR})
    if(USE_CLASS_MAKEFILE)
      target_link_libraries(${target_name} ${CLASS_OBJECT_FILES})
      target_link_libraries(${target_name} class_cpp)
      add_dependencies(${target_name} class_objects)
    else(USE_CLASS_MAKEFILE)
      target_link_libraries(${target_name} class)
    endif(USE_CLASS_MAKEFILE)
    target_compile_options(${target_name} PRIVATE "-DUSE_CLASS")
  endif(ENABLE_CLASS)
endmacro(target_setup_class)

# if(ENABLE_CLASS)
#   # test executable
#   add_executable(testTk
#     ${CMAKE_CURRENT_LIST_DIR}/class/cpp/testTk.cc)
#   target_setup_class(testTk)
# endif(ENABLE_CLASS)