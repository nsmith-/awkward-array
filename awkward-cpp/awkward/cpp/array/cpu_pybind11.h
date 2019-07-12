#pragma once
#include <pybind11/pybind11.h>
#include <pybind11/numpy.h>
#include <cinttypes>
#include <cstring>
#include "cpu_methods.h"
#include <stdio.h>

struct c_array py2c(py::array input) {
    char format[10];
    strcpy(format, input.request().format.c_str());
    struct c_array out = {
        input.request().ptr,
        input.request().itemsize,
        input.request().size,
        format,
        input.request().ndim,
        &input.request().shape[0],
        &input.request().strides[0]
    };
    return out;
}

int makeIntNative_CPU(py::array input) {
    if (!checkInt_CPU(&py2c(input))) {
        throw std::invalid_argument("Argument must be an int array");
    }
    if (!makeNative_CPU(&py2c(input))) {
        throw std::exception("Error in cpu_methods.h::makeNative_CPU");
    }
    return 1;
}
