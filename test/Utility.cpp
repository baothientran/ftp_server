#include <iostream>
#include "catch.hpp"
#include "Utility.h"


TEST_CASE("test normalize path", "Utility") {
    std::string path = "a/b//c";
    REQUIRE(normalizePath(path) == "a/b/c");

    path = "a/b//c/";
    REQUIRE(normalizePath(path) == "a/b/c");

    path = "/..";
    REQUIRE(normalizePath(path) == "");

    path = "a/b//c/";
    REQUIRE(normalizePath(path) == "a/b/c");

    path = "/a/b//c/";
    REQUIRE(normalizePath(path) == "a/b/c");

    path = "////a/b//c/";
    REQUIRE(normalizePath(path) == "a/b/c");

    path = "//../../a/b//c/";
    REQUIRE(normalizePath(path) == "a/b/c");

    path = "//../../a/../b//c/";
    REQUIRE(normalizePath(path) == "b/c");

    path = "//../../this is a dir/../b//c/";
    REQUIRE(normalizePath(path) == "b/c");

    path = "//../../this is a dir/../b/./c/";
    REQUIRE(normalizePath(path) == "b/c");

    path = "//../../this is a dir/../this is an another dir/./c/";
    REQUIRE(normalizePath(path) == "this is an another dir/c");

    path = "//../../this is a dir/a/b/c/../this is an another dir/./c/";
    REQUIRE(normalizePath(path) == "this is a dir/a/b/this is an another dir/c");
}
