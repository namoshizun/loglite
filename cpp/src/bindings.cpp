#include "api.hpp"
#include "config.hpp"

#include <Python.h>

#include <fmt/format.h>
#include <nlohmann/json.hpp>
#include <pybind11/pybind11.h>
#include <pybind11/stl.h>

namespace py = pybind11;
using namespace loglite;

namespace {

struct DateTimeTypes {
    py::object datetime;
    py::object date;
    py::object time;
};

// Lazy: first use is always under the GIL (e.g. push_to_backlog).
const DateTimeTypes& DatetimeTypes() {
    static const DateTimeTypes kTypes = [] {
        py::module_ m = py::module_::import("datetime");
        return DateTimeTypes{m.attr("datetime"), m.attr("date"), m.attr("time")};
    }();
    return kTypes;
}

[[nodiscard]] nlohmann::json PyObjectToJson(const py::handle& obj) {
    if (obj.is_none()) {
        return nullptr;
    }

    // bool before int: Python bool is a subclass of int.
    if (py::isinstance<py::bool_>(obj)) {
        return py::reinterpret_borrow<py::bool_>(obj).cast<bool>();
    }
    if (py::isinstance<py::int_>(obj)) {
        int overflow = 0;
        const long long v = PyLong_AsLongLongAndOverflow(obj.ptr(), &overflow);
        if (overflow != 0) {
            throw std::runtime_error("loglite._core: integer too large for JSON conversion");
        }
        if (v == -1 && PyErr_Occurred() != nullptr) {
            throw py::error_already_set();
        }
        return v;
    }
    if (py::isinstance<py::float_>(obj)) {
        return py::reinterpret_borrow<py::float_>(obj).cast<double>();
    }
    if (py::isinstance<py::str>(obj)) {
        return py::reinterpret_borrow<py::str>(obj).cast<std::string>();
    }
    if (py::isinstance<py::dict>(obj)) {
        nlohmann::json out = nlohmann::json::object();
        const py::dict d = py::reinterpret_borrow<py::dict>(obj);
        for (auto kv : d) {
            const py::handle k = kv.first;
            if (!py::isinstance<py::str>(k)) {
                throw std::runtime_error(
                    "loglite._core: dict keys must be str for JSON conversion");
            }
            const std::string key = py::reinterpret_borrow<py::str>(k).cast<std::string>();
            out[key] = PyObjectToJson(kv.second);
        }
        return out;
    }
    if (py::isinstance<py::list>(obj) || py::isinstance<py::tuple>(obj)) {
        nlohmann::json arr = nlohmann::json::array();
        const py::sequence seq = py::reinterpret_borrow<py::sequence>(obj);
        const Py_ssize_t n = py::len(seq);
        arr.get_ref<nlohmann::json::array_t&>().reserve(static_cast<size_t>(n));
        for (Py_ssize_t i = 0; i < n; ++i) {
            arr.push_back(PyObjectToJson(seq[i]));
        }
        return arr;
    }

    const auto& dt = DatetimeTypes();
    if (py::isinstance(obj, dt.datetime) || py::isinstance(obj, dt.date) ||
        py::isinstance(obj, dt.time)) {
        py::object iso = py::reinterpret_borrow<py::object>(obj).attr("isoformat")();
        return iso.cast<std::string>();
    }

    const py::object typ =
        py::reinterpret_borrow<py::object>(obj).attr("__class__").attr("__name__");
    throw std::runtime_error(fmt::format(
        "loglite._core: unsupported type '{}' in log payload (expected None, bool, int, float, "
        "str, dict, list, tuple, or datetime types)",
        typ.cast<std::string>()));
}

}  // namespace

PYBIND11_MODULE(_core, m) {
    m.doc() = "loglite C++ core — server, migrations, and backlog push";

    // ── Config.HarvesterDef ───────────────────────────────────────────────────
    py::class_<Config::HarvesterDef>(m, "HarvesterDef")
        .def_readonly("type", &Config::HarvesterDef::type)
        .def_readonly("name", &Config::HarvesterDef::name)
        .def_readonly("config", &Config::HarvesterDef::config);

    // ── Config ────────────────────────────────────────────────────────────────
    py::class_<Config>(m, "Config")
        .def_static(
            "from_file", [](const std::string& path) { return Config::from_file(path); },
            py::arg("path"))
        .def_readonly("host", &Config::host)
        .def_readonly("port", &Config::port)
        .def_readonly("harvesters", &Config::harvesters)
        .def_readonly("log_table_name", &Config::log_table_name);

    // ── Server ────────────────────────────────────────────────────────────────
    m.def(
        "run_server",
        [](const std::string& config_path, unsigned int thread_count) {
            py::gil_scoped_release release;
            RunServer(config_path, thread_count);
        },
        py::arg("config_path"), py::arg("thread_count") = 0u,
        "Start the server (blocks until shutdown).");

    m.def("stop_server", &StopServer, "Signal the running server to shut down.");

    // ── Migrations ────────────────────────────────────────────────────────────

    m.def(
        "rollout",
        [](const std::string& config_path, int start_version) {
            py::gil_scoped_release release;
            Rollout(config_path, start_version);
        },
        py::arg("config_path"), py::arg("start_version") = -1);

    m.def(
        "rollback",
        [](const std::string& config_path, int version, bool force) {
            py::gil_scoped_release release;
            Rollback(config_path, version, force);
        },
        py::arg("config_path"), py::arg("version"), py::arg("force") = false);

    // ── Backlog ───────────────────────────────────────────────────────────────
    m.def(
        "push_to_backlog",
        [](const py::dict& log) {
            // Convert while holding GIL, then release.
            auto entry = PyObjectToJson(log);
            py::gil_scoped_release release;
            PushToBacklog(std::move(entry));
        },
        py::arg("log"), "Push a log entry dict into the active server backlog (thread-safe).");
}
