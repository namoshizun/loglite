#include "api.hpp"
#include "config.hpp"
#include "harvesters/manager.hpp"

#include <nlohmann/json.hpp>
#include <pybind11/pybind11.h>
#include <pybind11/stl.h>

namespace py = pybind11;
using namespace loglite;

// Convert a Python dict/object to nlohmann::json via Python's stdlib json module.
// Keeps the binding free of extra C++ dependencies.
static nlohmann::json py_to_json(const py::object& obj) {
    auto json_mod = py::module_::import("json");
    std::string s = py::cast<std::string>(json_mod.attr("dumps")(obj));
    return nlohmann::json::parse(s);
}

PYBIND11_MODULE(_core, m) {
    m.doc() = "loglite C++ core — server, migrations, and backlog push";

    // ── Config.HarvesterDef ───────────────────────────────────────────────────
    py::class_<Config::HarvesterDef>(m, "HarvesterDef")
        .def_readonly("type",   &Config::HarvesterDef::type)
        .def_readonly("name",   &Config::HarvesterDef::name)
        .def_readonly("config", &Config::HarvesterDef::config);

    // ── Config ────────────────────────────────────────────────────────────────
    py::class_<Config>(m, "Config")
        .def_static("from_file", [](const std::string& path) {
            return Config::from_file(path);
        }, py::arg("path"))
        .def_readonly("host",         &Config::host)
        .def_readonly("port",         &Config::port)
        .def_readonly("harvesters",   &Config::harvesters)
        .def_readonly("log_table_name", &Config::log_table_name);

    // ── Server ────────────────────────────────────────────────────────────────
    //
    // run_server() blocks the calling thread.  We release the GIL so Python
    // harvester threads (which need it) can run concurrently.

    m.def("run_server",
          [](const std::string& config_path, unsigned int thread_count) {
              harvesters::HarvesterManager no_extra;
              py::gil_scoped_release release;
              RunServer(config_path, no_extra, thread_count);
          },
          py::arg("config_path"),
          py::arg("thread_count") = 0u,
          "Start the server (blocks until shutdown).");

    m.def("stop_server", &StopServer, "Signal the running server to shut down.");

    // ── Migrations ────────────────────────────────────────────────────────────

    m.def("rollout",
          [](const std::string& config_path, int start_version) {
              py::gil_scoped_release release;
              Rollout(config_path, start_version);
          },
          py::arg("config_path"),
          py::arg("start_version") = -1);

    m.def("rollback",
          [](const std::string& config_path, int version, bool force) {
              py::gil_scoped_release release;
              Rollback(config_path, version, force);
          },
          py::arg("config_path"),
          py::arg("version"),
          py::arg("force") = false);

    // ── Backlog ───────────────────────────────────────────────────────────────
    //
    // Called from Python harvester threads.  Convert the Python dict to JSON
    // and push into the active backlog.  The GIL is held by the caller; we
    // release it around the mutex-guarded Add() call.

    m.def("push_to_backlog",
          [](const py::dict& log) {
              // Convert while holding GIL (json.dumps needs it), then release.
              auto entry = py_to_json(log);
              py::gil_scoped_release release;
              PushToBacklog(std::move(entry));
          },
          py::arg("log"),
          "Push a log entry dict into the active server backlog (thread-safe).");
}
