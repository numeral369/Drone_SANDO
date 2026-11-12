#!/usr/bin/env python3
"""Patch a SANDO checkout (mit-acl/sando) to build with the open-source grb_compat solver.

Usage: python3 apply_open_solver.py <path/to/sando> <path/to/grb_compat>

* copies grb_compat into <sando>/deps/grb_compat
* CMakeLists.txt: option SANDO_USE_GUROBI (default OFF). OFF -> link grb_compat instead of Gurobi.
* setup.sh is NOT used by our build (remote/build.sh mirrors the upstream Dockerfile instead).
Idempotent: running twice is a no-op.
"""
import pathlib
import shutil
import sys

MARK = "# >>> grb_compat (open-source solver) >>>"


def main():
    sando = pathlib.Path(sys.argv[1]).resolve()
    compat = pathlib.Path(sys.argv[2]).resolve()
    dst = sando / "deps" / "grb_compat"
    if dst.exists():
        shutil.rmtree(dst)
    shutil.copytree(compat, dst, ignore=shutil.ignore_patterns("build", "tests"))
    # colcon must not treat it as a package
    (dst / "COLCON_IGNORE").write_text("")

    cm = sando / "CMakeLists.txt"
    txt = cm.read_text()
    if MARK in txt:
        print("CMakeLists.txt already patched")
        return
    old_find = """# Gurobi
list(APPEND CMAKE_MODULE_PATH "${CMAKE_SOURCE_DIR}")
find_package(GUROBI REQUIRED)

# check if gurobi is found
if(GUROBI_FOUND)
message(STATUS "GUROBI FOUND")
else(GUROBI_FOUND)
message(FATAL_ERROR "GUROBI NOT FOUND!")
endif(GUROBI_FOUND)
"""
    new_find = f"""{MARK}
option(SANDO_USE_GUROBI "Use the commercial Gurobi solver instead of open-source grb_compat" OFF)
if(SANDO_USE_GUROBI)
  list(APPEND CMAKE_MODULE_PATH "${{CMAKE_SOURCE_DIR}}")
  find_package(GUROBI REQUIRED)
else()
  message(STATUS "SANDO: using open-source grb_compat solver (no Gurobi)")
  add_library(grb_compat STATIC deps/grb_compat/src/grb_compat.cpp)
  target_include_directories(grb_compat PUBLIC ${{CMAKE_CURRENT_SOURCE_DIR}}/deps/grb_compat/include ${{EIGEN3_INCLUDE_DIR}})
  # SANDO compiles with -ffast-math, which assumes no infinities; the QP solver relies on IEEE inf.
  target_compile_options(grb_compat PRIVATE -O3 -fno-fast-math)
  # No LTO for this target, so -ffast-math from SANDO's objects can never be merged into the solver code.
  set_target_properties(grb_compat PROPERTIES POSITION_INDEPENDENT_CODE ON INTERPROCEDURAL_OPTIMIZATION OFF)
  set(GUROBI_INCLUDE_DIRS ${{CMAKE_CURRENT_SOURCE_DIR}}/deps/grb_compat/include)
endif()
# <<< grb_compat <<<
"""
    old_libs = """FILE(GLOB GurobiSOFiles $ENV{GUROBI_HOME}/lib/libgurobi*[0-9].so) #files that are start with libgurobi and end with number.so
set(GUROBI_LIBRARIES "$ENV{GUROBI_HOME}/lib/libgurobi_c++.a;${GurobiSOFiles};$ENV{GUROBI_HOME}/lib/" )
"""
    new_libs = """if(SANDO_USE_GUROBI)
FILE(GLOB GurobiSOFiles $ENV{GUROBI_HOME}/lib/libgurobi*[0-9].so) #files that are start with libgurobi and end with number.so
set(GUROBI_LIBRARIES "$ENV{GUROBI_HOME}/lib/libgurobi_c++.a;${GurobiSOFiles};$ENV{GUROBI_HOME}/lib/" )
else()
set(GUROBI_LIBRARIES grb_compat)
endif()
"""
    # Portable CPU target: the build snapshot may be restored on a different DO host CPU,
    # and -march=native could then crash with "illegal instruction".
    old_march = '-O3 -march=native -DNDEBUG'
    new_march = '-O3 -march=x86-64-v2 -mtune=generic -DNDEBUG'
    for old, new in ((old_find, new_find), (old_libs, new_libs), (old_march, new_march)):
        if old not in txt:
            sys.exit(f"ERROR: expected block not found in CMakeLists.txt:\n{old}")
        txt = txt.replace(old, new, 1)
    cm.write_text(txt)
    print("CMakeLists.txt patched (SANDO_USE_GUROBI=OFF by default)")


if __name__ == "__main__":
    main()
