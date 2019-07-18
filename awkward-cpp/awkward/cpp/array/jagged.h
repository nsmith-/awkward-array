#pragma once
#include <pybind11/pybind11.h>
#include <pybind11/numpy.h>
#include <pybind11/stl.h>
#include <cinttypes>
#include <stdexcept>
#include <sstream>
#include "util.h"
#include "any.h"
#include "numpytypes.h"
#include "cpu_methods.h"
#include "cpu_pybind11.h"

#include <stdio.h> // for debugging purposes

namespace py = pybind11;

class JaggedArray : public AwkwardArray {
public:
    py::array_t<std::int64_t> starts,
                              stops;
    AnyArray*                 content;

    py::object unwrap() {
        return py::cast(this);
    }

    AnyArray* get_content() { return content; }

    py::object python_get_content() {
        return content->unwrap();
    }

    void set_content(AnyArray* content_) {
        content = content_;
    }

    void python_set_content(py::object content_) {
        try {
            set_content(content_.cast<JaggedArray*>());
            return;
        }
        catch (py::cast_error e) { }
        try {
            set_content(getNumpyArray_t(content_.cast<py::array>()));
            return;
        }
        catch (py::cast_error e) {
            throw std::invalid_argument("Invalid type for JaggedArray.content");
        }
    }

    py::array_t<std::int64_t> get_starts() { return starts; }

    void set_starts(py::array starts_) {
        makeIntNative_CPU(starts_);
        starts_ = starts_.cast<py::array_t<std::int64_t>>();
        if (starts_.request().ndim < 1) {
            throw std::domain_error("starts must have at least 1 dimension");
        }
        if (!checkNonNegative_CPU(py2c(starts_.request()))) {
            throw std::invalid_argument("starts must have all non-negative values");
        }
        starts = starts_;
    }

    void python_set_starts(py::object input) {
        py::array starts_ = input.cast<py::array>();
        set_starts(starts_);
    }

    py::array_t<std::int64_t> get_stops() { return stops; }

    void set_stops(py::array stops_) {
        makeIntNative_CPU(stops_);
        stops_ = stops_.cast<py::array_t<std::int64_t>>();
        if (stops_.request().ndim < 1) {
            throw std::domain_error("stops must have at least 1 dimension");
        }
        if (!checkNonNegative_CPU(py2c(stops_.request()))) {
            throw std::invalid_argument("stops must have all non-negative values");
        }
        stops = stops_;
    }
    
    void python_set_stops(py::object input) {
        py::array stops_ = input.cast<py::array>();
        set_stops(stops_);
    }

    bool check_validity() {
        py::buffer_info starts_info = starts.request();
        py::buffer_info stops_info = stops.request();
        if (starts_info.size > stops_info.size) {
            throw std::invalid_argument("starts must have the same (or shorter) length than stops");
        }
        if (starts_info.ndim != stops_info.ndim) {
            throw std::domain_error("starts and stops must have the same dimensionality");
        }
        int N_starts = starts_info.strides[0] / starts_info.itemsize;
        int N_stops = stops_info.strides[0] / stops_info.itemsize;
        std::int64_t starts_max = 0;
        std::int64_t stops_max = 0;
        auto starts_ptr = (std::int64_t*)starts_info.ptr;
        auto stops_ptr = (std::int64_t*)stops_info.ptr;
        for (ssize_t i = 0; i < starts_info.size; i++) {
            if (stops_ptr[i * N_stops] < starts_ptr[i * N_starts]) {
                throw std::invalid_argument("stops must be greater than or equal to starts");
            }
            if (starts_ptr[i * N_starts] > starts_max) {
                starts_max = starts_ptr[i * N_starts];
            }
            if (stops_ptr[i * N_stops] > stops_max) {
                stops_max = stops_ptr[i * N_stops];
            }
        }
        if (starts_info.size > 0) {
            if (starts_max >= content->len()) {
                throw std::invalid_argument("The maximum of starts for non-empty elements must be less than the length of content");
            }
            if (stops_max > content->len()) {
                throw std::invalid_argument("The maximum of stops for non-empty elements must be less than or equal to the length of content");
            }
        }
        return true;
    }

    JaggedArray(py::object starts_, py::object stops_, py::object content_) {
        python_set_starts(starts_);
        python_set_stops(stops_);
        python_set_content(content_);
        check_validity();
    }

    JaggedArray(py::array starts_, py::array stops_, AnyArray* content_) {
        set_starts(starts_);
        set_stops(stops_);
        set_content(content_);
        check_validity();
    }

    static JaggedArray* fromoffsets(py::array offsets, AnyArray* content_) {
        makeIntNative_CPU(offsets);
        py::array_t<std::int64_t> temp = offsets.cast<py::array_t<std::int64_t>>();
        ssize_t length = temp.request().size;
        if (length < 1) {
            throw std::invalid_argument("offsets must have at least one element");
        }
        if (temp.request().ndim > 1) {
            throw std::domain_error("offsets must be one-dimensional");
        }
        return new JaggedArray(
            slice_numpy(temp, 0, length - 1),
            slice_numpy(temp, 1, length - 1),
            content_
        );
    }

    static JaggedArray* python_fromoffsets(py::object input, py::object content_) {
        py::array offsets = input.cast<py::array>();
        try {
            return fromoffsets(offsets, content_.cast<JaggedArray*>());
        }
        catch (py::cast_error e) { }
        try {
            return fromoffsets(offsets, getNumpyArray_t(content_.cast<py::array>()));
        }
        catch (py::cast_error e) {
            throw std::invalid_argument("Invalid type for JaggedArray.content");
        }
    }

    static JaggedArray* fromcounts(py::array counts, AnyArray* content_) {
        return fromoffsets(counts2offsets(counts), content_);
    }

    static JaggedArray* python_fromcounts(py::object input, py::object content_) {
        py::array counts = input.cast<py::array>();
        try {
            return fromcounts(counts, content_.cast<JaggedArray*>());
        }
        catch (py::cast_error e) { }
        try {
            return fromcounts(counts, getNumpyArray_t(content_.cast<py::array>()));
        }
        catch (py::cast_error e) {
            throw std::invalid_argument("Invalid type for JaggedArray.content");
        }
    }

    static AnyArray* fromiter_helper(py::tuple input) {
        if (input.size() == 0) {
            return getNumpyArray_t(py::array_t<std::int32_t>(0));
        }
        try {
            input[0].cast<py::tuple>();
            return fromiter(input);
        }
        catch (std::exception e) {
            py::array out = input.cast<py::array>();
            return getNumpyArray_t(out);
        }
    }

    static JaggedArray* fromiter(py::object input) {
        py::tuple iter = input.cast<py::tuple>();
        auto counts = py::array_t<std::int64_t>(iter.size());
        auto counts_ptr = (std::int64_t*)counts.request().ptr;

        py::list contentList;

        if (iter.size() == 0) {
            return fromcounts(counts, getNumpyArray_t(py::array_t<std::int32_t>(0)));
        }
        for (size_t i = 0; i < iter.size(); i++) {
            py::tuple thisIter;
            try {
                thisIter = iter[i].cast<py::tuple>();
            }
            catch (std::exception e) {
                throw std::invalid_argument("jagged iterable must contain only iterables to make a jagged array");
            }
            counts_ptr[i] = (std::int64_t)thisIter.size();
            for (size_t i = 0; i < thisIter.size(); i++) {
                contentList.append(thisIter[i]);
            }
        }
        auto content_out = py::tuple(contentList);
        return fromcounts(counts, fromiter_helper(content_out));
    }

    static JaggedArray* fromparents(py::array parents, AnyArray* content_, ssize_t length = -1) {
        if (parents.request().ndim != 1 || parents.request().size != content_->len()) {
            throw std::invalid_argument("parents array must be one-dimensional with the same length as content");
        }
        auto startsstops = parents2startsstops(parents, length);
        return new JaggedArray(startsstops[0], startsstops[1], content_);
    }

    static JaggedArray* python_fromparents(py::object input, py::object content_, ssize_t length = -1) {
        py::array parents = input.cast<py::array>();
        try {
            return fromparents(parents, content_.cast<JaggedArray*>(), length);
        }
        catch (py::cast_error e) { }
        try {
            return fromparents(parents, getNumpyArray_t(content_.cast<py::array>()), length);
        }
        catch (py::cast_error e) {
            throw std::invalid_argument("Invalid type for JaggedArray.content");
        }
    }

    static JaggedArray* fromuniques(py::array uniques, AnyArray* content_) {
        if (uniques.request().ndim != 1 || uniques.request().size != content_->len()) {
            throw std::invalid_argument("uniques array must be one-dimensional with the same length as content");
        }
        auto offsetsparents = uniques2offsetsparents(uniques);
        return fromoffsets(offsetsparents[0], content_);
    }

    static JaggedArray* python_fromuniques(py::object input, py::object content_) {
        py::array uniques = input.cast<py::array>();
        try {
            return fromuniques(uniques, content_.cast<JaggedArray*>());
        }
        catch (py::cast_error e) { }
        try {
            return fromuniques(uniques, getNumpyArray_t(content_.cast<py::array>()));
        }
        catch (py::cast_error e) {
            throw std::invalid_argument("Invalid type for JaggedArray.content");
        }
    }

    static JaggedArray* fromjagged(JaggedArray* jagged) {
        return new JaggedArray(jagged->get_starts(), jagged->get_stops(), jagged->get_content());
    }

    JaggedArray* copy() {
        return new JaggedArray(starts, stops, content);
    }

    AnyArray* deepcopy() {
        return new JaggedArray(
            pyarray_deepcopy(starts),
            pyarray_deepcopy(stops),
            content->deepcopy()
        );
    }

    JaggedArray* python_deepcopy() {
        return (JaggedArray*)deepcopy();
    }

    static py::array_t<std::int64_t> offsets2parents(py::array offsets) {
        makeIntNative_CPU(offsets);
        offsets = offsets.cast<py::array_t<std::int64_t>>();
        py::buffer_info offsets_info = offsets.request();
        if (offsets_info.size <= 0) {
            throw std::invalid_argument("offsets must have at least one element");
        }
        auto offsets_ptr = (std::int64_t*)offsets_info.ptr;
        int N = offsets_info.strides[0] / offsets_info.itemsize;

        ssize_t parents_length = (ssize_t)offsets_ptr[(offsets_info.size - 1) * N];
        auto parents = py::array_t<std::int64_t>(parents_length);

        if (!offsets2parents_CPU(py2c(offsets.request()), py2c(parents.request()))) {
            throw std::invalid_argument("Error in cpu_methods.h::offsets2parents_CPU");
        }
        return parents;
    }

    static py::array_t<std::int64_t> python_offsets2parents(py::object offsetsIter) {
        py::array offsets = offsetsIter.cast<py::array>();
        return offsets2parents(offsets);
    }

    static py::array_t<std::int64_t> counts2offsets(py::array counts) {
        makeIntNative_CPU(counts);
        counts = counts.cast<py::array_t<std::int64_t>>();
        auto offsets = py::array_t<std::int64_t>(counts.request().size + 1);
        if (!counts2offsets_CPU(py2c(counts.request()), py2c(offsets.request()))) {
            throw std::invalid_argument("Error in cpu_methods.h::counts2offsets_CPU");
        }
        return offsets;
    }

    static py::array_t<std::int64_t> python_counts2offsets(py::object countsIter) {
        py::array counts = countsIter.cast<py::array>();
        return counts2offsets(counts);
    }

    static py::array_t<std::int64_t> startsstops2parents(py::array starts_, py::array stops_) {
        makeIntNative_CPU(starts_);
        makeIntNative_CPU(stops_);
        starts_ = starts_.cast<py::array_t<std::int64_t>>();
        stops_ = stops_.cast<py::array_t<std::int64_t>>();

        std::int64_t max = 0;
        getMax_CPU(stops_, &max);
        auto parents = py::array_t<std::int64_t>((ssize_t)max);

        if (!startsstops2parents_CPU(py2c(starts_.request()), py2c(stops_.request()), py2c(parents.request()))) {
            throw std::invalid_argument("Error in cpu_methods.h::startsstops2parents_CPU");
        }
        return parents;
    }

    static py::array_t<std::int64_t> python_startsstops2parents(py::object startsIter, py::object stopsIter) {
        py::array starts_ = startsIter.cast<py::array>();
        py::array stops_ = stopsIter.cast<py::array>();
        return startsstops2parents(starts_, stops_);
    }

    static py::tuple parents2startsstops(py::array parents, std::int64_t length = -1) {
        makeIntNative_CPU(parents);
        parents = parents.cast<py::array_t<std::int64_t>>();

        if (length < 0) {
            length = 0;
            getMax_CPU(parents, &length);
            length++;
        }
        auto starts_ = py::array_t<std::int64_t>((ssize_t)length);
        auto stops_ = py::array_t<std::int64_t>((ssize_t)length);

        if (!parents2startsstops_CPU(py2c(parents.request()), py2c(starts_.request()), py2c(stops_.request()))) {
            throw std::invalid_argument("Error in cpu_methods.h::parents2startsstops_CPU");
        }
        py::list temp;
        temp.append(starts_);
        temp.append(stops_);
        py::tuple out(temp);
        return out;
    }

    static py::tuple python_parents2startsstops(py::object parentsIter, std::int64_t length = -1) {
        py::array parents = parentsIter.cast<py::array>();
        return parents2startsstops(parents, length);
    }

    static py::tuple uniques2offsetsparents(py::array uniques) {
        makeIntNative_CPU(uniques);
        uniques = uniques.cast<py::array_t<std::int64_t>>();

        ssize_t tempLength = 0;
        if (uniques.request().size > 0) {
            tempLength = uniques.request().size - 1;
        }

        auto tempArray = py::array_t<std::int8_t>(tempLength);
        ssize_t countLength = 0;
        if (!uniques2offsetsparents_generateTemparray_CPU(py2c(uniques.request()), py2c(tempArray.request()), &countLength)) {
            throw std::invalid_argument("Error in cpu_methods.h::uniques2offsetsparents_generateTempArray_CPU");
        }
        auto offsets = py::array_t<std::int64_t>(countLength + 2);
        auto parents = py::array_t<std::int64_t>(uniques.request().size);
        if (!uniques2offsetsparents_CPU(countLength, py2c(tempArray.request()), py2c(offsets.request()), py2c(parents.request()))) {
            throw std::invalid_argument("Error in cpu_methods.h::uniques2offsetsparents_CPU");
        }

        py::list temp;
        temp.append(offsets);
        temp.append(parents);
        py::tuple out(temp);
        return out;
    }

    static py::tuple python_uniques2offsetsparents(py::object uniquesIter) {
        py::array uniques = uniquesIter.cast<py::array>();
        return uniques2offsetsparents(uniques);
    }

    ssize_t len() {
        return starts.request().size;
    }

    AnyArray* getitem(ssize_t start, ssize_t length, ssize_t step = 1) {
        if (step == 0) {
            throw std::invalid_argument("slice step cannot be 0");
        }
        if (length < 0) {
            throw std::invalid_argument("slice length cannot be less than 0");
        }
        if (start < 0 || start >= len() || (length > 0 &&
            (start + ((length - 1) * step) > len() ||
            start + ((length - 1) * step) < -1))) {
            throw std::out_of_range("getitem must be in the bounds of the array.");
        }
        auto newStarts = py::array_t<std::int64_t>(length);
        py::buffer_info newStarts_info = newStarts.request();
        auto newStarts_ptr = (std::int64_t*)newStarts_info.ptr;

        py::buffer_info starts_info = starts.request();
        auto starts_ptr = (std::int64_t*)starts_info.ptr;
        int N_starts = starts_info.strides[0] / starts_info.itemsize;

        auto newStops = py::array_t<std::int64_t>(length);
        py::buffer_info newStops_info = newStops.request();
        auto newStops_ptr = (std::int64_t*)newStops_info.ptr;

        py::buffer_info stops_info = stops.request();
        auto stops_ptr = (std::int64_t*)stops_info.ptr;
        int N_stops = stops_info.strides[0] / stops_info.itemsize;

        ssize_t newIndex = 0;
        for (ssize_t i = 0; i < length; i++) {
            newStarts_ptr[newIndex] = starts_ptr[start + (i * step * N_starts)];
            newStops_ptr[newIndex++] = stops_ptr[start + (i * step * N_stops)];
        }

        return new JaggedArray(newStarts, newStops, content);
    }

    py::object python_getitem(py::slice input) {
        size_t start, stop, step, slicelength;
        if (!input.compute(len(), &start, &stop, &step, &slicelength)) {
            throw py::error_already_set();
        }
        return getitem((ssize_t)start, (ssize_t)slicelength, (ssize_t)step)->unwrap();
    }

    AnyArray* getitem(ssize_t index) {
        py::buffer_info starts_info = starts.request();
        py::buffer_info stops_info = stops.request();
        if (starts_info.size > stops_info.size) {
            throw std::out_of_range("starts must have the same or shorter length than stops");
        }
        if (index > starts_info.size || index < 0) {
            throw std::out_of_range("getitem must be in the bounds of the array");
        }
        if (starts_info.ndim != stops_info.ndim) {
            throw std::domain_error("starts and stops must have the same dimensionality");
        }
        int N_starts = starts_info.strides[0] / starts_info.itemsize;
        int N_stops = stops_info.strides[0] / stops_info.itemsize;
        ssize_t start = (ssize_t)((std::int64_t*)starts_info.ptr)[index * N_starts];
        ssize_t stop = (ssize_t)((std::int64_t*)stops_info.ptr)[index * N_stops];

        return content->getitem(start, stop - start);
    }

    py::object python_getitem(ssize_t index) {
        if (index < 0) {
            index += starts.request().size;
        }
        return getitem(index)->unwrap();
    }

    JaggedArray* boolarray_getitem(py::array input) {
        ssize_t length = input.request().size;
        if (length != len()) {
            throw std::invalid_argument("bool array length must be equal to jagged array length");
        }
        auto array_ptr = (bool*)input.request().ptr;

        py::list tempStarts;
        py::list tempStops;

        py::buffer_info starts_info = starts.request();
        auto starts_ptr = (std::int64_t*)starts_info.ptr;
        int N_starts = starts_info.strides[0] / starts_info.itemsize;

        py::buffer_info stops_info = stops.request();
        auto stops_ptr = (std::int64_t*)stops_info.ptr;
        int N_stops = stops_info.strides[0] / stops_info.itemsize;

        for (ssize_t i = 0; i < length; i++) {
            if (array_ptr[i]) {
                tempStarts.append(starts_ptr[i * N_starts]);
                tempStops.append(stops_ptr[i * N_stops]);
            }
        }
        py::array_t<std::int64_t> outStarts = tempStarts.cast<py::array_t<std::int64_t>>();
        py::array_t<std::int64_t> outStops = tempStops.cast<py::array_t<std::int64_t>>();
        return new JaggedArray(outStarts, outStops, content);
    }

    JaggedArray* intarray_getitem(py::array input) {
        makeIntNative_CPU(input);
        input = input.cast<py::array_t<std::int64_t>>();
        py::buffer_info array_info = input.request();
        auto array_ptr = (std::int64_t*)array_info.ptr;

        auto newStarts = py::array_t<std::int64_t>(array_info.size);
        auto newStarts_ptr = (std::int64_t*)newStarts.request().ptr;

        py::buffer_info starts_info = starts.request();
        auto starts_ptr = (std::int64_t*)starts_info.ptr;
        int N_starts = starts_info.strides[0] / starts_info.itemsize;

        auto newStops = py::array_t<std::int64_t>(array_info.size);
        auto newStops_ptr = (std::int64_t*)newStops.request().ptr;

        py::buffer_info stops_info = stops.request();
        auto stops_ptr = (std::int64_t*)stops_info.ptr;
        int N_stops = stops_info.strides[0] / stops_info.itemsize;

        for (ssize_t i = 0; i < array_info.size; i++) {
            std::int64_t here = array_ptr[i];
            if (here < 0 || here >= len()) {
                throw std::invalid_argument("int array indices must be within the bounds of the jagged array");
            }
            newStarts_ptr[i] = starts_ptr[N_starts * here];
            newStops_ptr[i] = stops_ptr[N_stops * here];
        }
        return new JaggedArray(newStarts, newStops, content);
    }

    JaggedArray* getitem(py::array input) {
        if (input.request().format.find("?") != std::string::npos) {
            return boolarray_getitem(input);
        }
        return intarray_getitem(input);
    }

    py::object python_getitem(py::array input) {
        return getitem(input)->unwrap();
    }

    py::object tolist() {
        py::list out;
        for (ssize_t i = 0; i < len(); i++) {
            out.append(getitem(i)->tolist());
        }
        return out;
    }

    std::string str() {
        std::string out;

        py::buffer_info starts_info = starts.request();
        auto starts_ptr = (std::int64_t*)starts_info.ptr;

        py::buffer_info stops_info = stops.request();
        auto stops_ptr = (std::int64_t*)stops_info.ptr;

        out.reserve(starts_info.size * 20);

        ssize_t limit = starts_info.size;
        if (limit > stops_info.size) {
            throw std::out_of_range("starts must have the same or shorter length than stops");
        }
        out.append("[");
        for (ssize_t i = 0; i < limit; i++) {
            if (i != 0) {
                out.append(" ");
            }
            out.append((getitem(i))->str());
        }
        out.append("]");
        out.shrink_to_fit();
        return out;
    }

    std::string repr() {
        std::stringstream stream;
        stream << std::hex << (long)this;
        return "<JaggedArray " + str() + " at 0x" + stream.str() + ">";
    }

    class JaggedArrayIterator {
    private:
        JaggedArray* thisArray;
        ssize_t      iter_index;

    public:
        JaggedArrayIterator(JaggedArray* thisArray_) {
            iter_index = 0;
            thisArray = thisArray_;
        }

        JaggedArrayIterator* iter() {
            return this;
        }

        py::object next() {
            if (iter_index >= thisArray->len()) {
                throw py::stop_iteration();
            }
            return thisArray->getitem(iter_index++)->unwrap();
        }
    };

    JaggedArrayIterator* iter() {
        return new JaggedArrayIterator(this);
    }
};
