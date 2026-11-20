// pybind11 bindings exposing the ByteGuard C++17 engine to the Python web app.
#include <pybind11/pybind11.h>
#include <pybind11/stl.h>

#include "../cpp/session.cpp"

namespace py = pybind11;

PYBIND11_MODULE(byteguard_cpp, m) {
    m.doc() = "ByteGuard C++17 LDPC Tanner Graph engine";
    m.attr("BIT_UNKNOWN") = BIT_UNKNOWN;

    py::class_<Image>(m, "Image")
        .def(py::init<>())
        .def(py::init([](int width, int height, int channels, py::bytes raw) {
                 string buf = raw;
                 size_t expected = (size_t)width * height * channels;
                 if (width <= 0 || height <= 0 || channels <= 0 || buf.size() != expected) {
                     throw invalid_argument("Invalid image dimensions or pixel buffer size");
                 }
                 Image img{width, height, channels, vector<uint8_t>(expected)};
                 memcpy(img.pixels.data(), buf.data(), expected);
                 return img;
             }),
             py::arg("width"), py::arg("height"), py::arg("channels"), py::arg("pixels"))
        .def_readwrite("width", &Image::width)
        .def_readwrite("height", &Image::height)
        .def_readwrite("channels", &Image::channels)
        .def("pixels_bytes", [](Image& img) {
            return py::bytes((const char*)img.pixels.data(), img.pixels.size());
        });

    py::class_<BrushStroke>(m, "BrushStroke")
        .def(py::init<double, double, double>(),
             py::arg("x") = 0.0, py::arg("y") = 0.0, py::arg("radius") = 8.0)
        .def_readwrite("x", &BrushStroke::x)
        .def_readwrite("y", &BrushStroke::y)
        .def_readwrite("radius", &BrushStroke::radius);

    py::class_<Metrics>(m, "Metrics")
        .def_readonly("protected_correct_pct", &Metrics::protectedCorrectPct)
        .def_readonly("unprotected_correct_pct", &Metrics::unprotectedCorrectPct)
        .def_readonly("damaged_protected_pct", &Metrics::damagedProtectedPct)
        .def_readonly("triple_copy_correct_pct", &Metrics::tripleCopyCorrectPct)
        .def_readonly("code_rate", &Metrics::codeRate)
        .def_readonly("ldpc_overhead_ratio", &Metrics::ldpcOverheadRatio)
        .def_readonly("triple_copy_overhead_ratio", &Metrics::tripleCopyOverheadRatio)
        .def_readonly("original_bytes", &Metrics::originalBytes)
        .def_readonly("protected_bytes", &Metrics::protectedBytes)
        .def_readonly("triple_copy_bytes", &Metrics::tripleCopyBytes)
        .def_readonly("total_data_bits", &Metrics::totalDataBits)
        .def_readonly("correct_data_bits", &Metrics::correctDataBits)
        .def_readonly("remaining_unknown_bits", &Metrics::remainingUnknownBits)
        .def_readonly("failed_checks", &Metrics::failedChecks)
        .def_readonly("total_checks", &Metrics::totalChecks)
        .def_readonly("total_blocks", &Metrics::totalBlocks)
        .def_readonly("fully_repaired_blocks", &Metrics::fullyRepairedBlocks)
        .def_readonly("stopping_set_bits_total", &Metrics::stoppingSetBitsTotal)
        .def_readonly("round", &Metrics::round);

    py::class_<BlockGraphSnapshot>(m, "BlockGraphSnapshot")
        .def_readonly("block_index", &BlockGraphSnapshot::blockIndex)
        .def_readonly("round", &BlockGraphSnapshot::round)
        .def_readonly("bits", &BlockGraphSnapshot::bits)
        .def_readonly("original_bits", &BlockGraphSnapshot::originalBits)
        .def_readonly("check_failed", &BlockGraphSnapshot::checkFailed)
        .def_readonly("stopping_set", &BlockGraphSnapshot::stoppingSet)
        .def_readonly("newly_fixed_vars", &BlockGraphSnapshot::newlyFixedVars);

    py::class_<Snapshot>(m, "Snapshot")
        .def_readonly("round", &Snapshot::round)
        .def_readonly("phase", &Snapshot::phase)
        .def_readonly("repaired_image", &Snapshot::repairedImage)
        .def_readonly("metrics", &Snapshot::metrics)
        .def_readonly("inspected_block", &Snapshot::inspectedBlock);

    py::class_<TannerGraph>(m, "TannerGraph")
        .def(py::init<int, int>(), py::arg("n") = 0, py::arg("m") = 0)
        .def_static("hamming74", &TannerGraph::hamming74)
        .def_static("from_incidence_matrix", &TannerGraph::fromIncidenceMatrix)
        .def("add_edge", &TannerGraph::addEdge)
        .def("num_vars", &TannerGraph::numVars)
        .def("num_checks", &TannerGraph::numChecks)
        .def("num_edges", &TannerGraph::numEdges)
        .def("degree", &TannerGraph::degree)
        .def("check_degree", &TannerGraph::checkDegree)
        .def("var_neighbors", &TannerGraph::varNeighbors, py::return_value_policy::reference_internal)
        .def("chk_neighbors", &TannerGraph::chkNeighbors, py::return_value_policy::reference_internal)
        .def("incidence_matrix", &TannerGraph::incidenceMatrix)
        .def("is_check_satisfied", &TannerGraph::isCheckSatisfied)
        .def("failed_checks", &TannerGraph::failedChecks)
        .def("is_valid_codeword", &TannerGraph::isValidCodeword);

    py::class_<GraphAnalyzer>(m, "GraphAnalyzer")
        .def(py::init<TannerGraph&>(), py::keep_alive<1, 2>())
        .def("girth", &GraphAnalyzer::girth)
        .def("is_connected", &GraphAnalyzer::isConnected)
        .def("stopping_set", &GraphAnalyzer::stoppingSet)
        .def("check_colors", &GraphAnalyzer::checkColors)
        .def("layer_schedule", &GraphAnalyzer::layerSchedule);

    py::class_<PEGBuilder>(m, "PEGBuilder")
        .def(py::init<int>(), py::arg("seed") = 42)
        .def("build", &PEGBuilder::build)
        .def("bfs_depth", &PEGBuilder::bfsDepth);

    py::class_<Encoder>(m, "Encoder")
        .def(py::init<TannerGraph&>())
        .def("num_vars", &Encoder::numVars)
        .def("num_checks", &Encoder::numChecks)
        .def("num_info_bits", &Encoder::numInfoBits)
        .def("num_parity_bits", &Encoder::numParityBits)
        .def("info_positions", &Encoder::infoPositions, py::return_value_policy::reference_internal)
        .def("parity_positions", &Encoder::parityPositions, py::return_value_policy::reference_internal)
        .def("topo_order", &Encoder::topoOrder, py::return_value_policy::reference_internal)
        .def("encode", &Encoder::encode)
        .def("extract", &Encoder::extract);

    py::class_<PeelingDecoder>(m, "PeelingDecoder")
        .def(py::init<TannerGraph&, int>(),
             py::arg("graph"), py::arg("max_rounds") = 100, py::keep_alive<1, 2>())
        .def("reset", &PeelingDecoder::reset)
        .def("step", [](PeelingDecoder& self, Bits bits) {
            bool changed = self.step(bits);
            return make_pair(changed, bits);
        })
        .def("last_fixed_vars", &PeelingDecoder::lastFixedVars,
             py::return_value_policy::reference_internal)
        .def("remaining_unknowns", &PeelingDecoder::remainingUnknowns);

    py::class_<BitFlipDecoder>(m, "BitFlipDecoder")
        .def(py::init<TannerGraph&, int>(),
             py::arg("graph"), py::arg("max_rounds") = 25, py::keep_alive<1, 2>())
        .def("reset", &BitFlipDecoder::reset)
        .def("step", [](BitFlipDecoder& self, Bits bits) {
            bool changed = self.step(bits);
            return make_pair(changed, bits);
        })
        .def("last_fixed_vars", &BitFlipDecoder::lastFixedVars,
             py::return_value_policy::reference_internal)
        .def("layers", &BitFlipDecoder::layers, py::return_value_policy::reference_internal);

    py::class_<Session>(m, "Session")
        .def(py::init<Image, int, int, uint32_t>(),
             py::arg("image"), py::arg("n") = 576, py::arg("m") = 288, py::arg("graph_seed") = 42)
        .def_static("with_graph", &Session::withGraph)
        .def("protect", &Session::protect)
        .def("scramble", &Session::scramble,
             py::arg("mode"), py::arg("level"), py::arg("seed") = 42,
             py::arg("strokes") = vector<BrushStroke>{})
        .def("repair_step", &Session::repairStep)
        .def("repair_all", &Session::repairAll, py::arg("max_rounds") = 25)
        .def("metrics", &Session::metrics)
        .def("history", &Session::history, py::return_value_policy::reference_internal)
        .def("graph", &Session::graph, py::return_value_policy::reference_internal)
        .def("encoder", &Session::encoder, py::return_value_policy::reference_internal)
        .def("original_image", &Session::originalImage, py::return_value_policy::reference_internal)
        .def("unprotected_damaged_image", &Session::unprotectedDamagedImage,
             py::return_value_policy::reference_internal)
        .def("total_blocks", &Session::totalBlocks)
        .def("inspected_block_idx", &Session::inspectedBlockIdx)
        .def("set_inspected_block", &Session::setInspectedBlock)
        .def("is_repair_finished", &Session::isRepairFinished)
        .def("graph_girth", &Session::graphGirth)
        .def("graph_is_connected", &Session::graphIsConnected)
        .def("check_colors", &Session::checkColors)
        .def("layer_schedule", &Session::layerSchedule)
        .def("graph_summary", [](Session& s) {
            py::dict d;
            auto& g = s.graph();
            auto layers = s.layerSchedule();
            d["n"] = g.numVars();
            d["m"] = g.numChecks();
            d["k"] = s.encoder().numInfoBits();
            d["r"] = s.encoder().numParityBits();
            d["edges"] = g.numEdges();
            d["girth"] = s.graphGirth();
            d["connected"] = s.graphIsConnected();
            d["num_colors"] = (int)layers.size();
            d["total_blocks"] = s.totalBlocks();
            return d;
        });
}
