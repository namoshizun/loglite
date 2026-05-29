from conan import ConanFile


class LogliteConan(ConanFile):
    name = "loglite"
    settings = "os", "arch", "compiler", "build_type"
    generators = "CMakeDeps", "CMakeToolchain"
    options = {"with_tests": [True, False], "with_python": [True, False]}
    default_options = {
        "boost/*:header_only": True,
        "date/*:tz_db": "system",
        "with_tests": True,
        "with_python": False,
    }

    def requirements(self):
        self.requires("boost/1.90.0")
        self.requires("cli11/2.6.2")
        self.requires("nlohmann_json/3.12.0")
        self.requires(
            "sqlite3/3.53.0",
            options={
                "build_executable": False,
                "omit_load_extension": True,
                "enable_fts3": False,
                "enable_fts4": False,
                "enable_fts5": False,
                "enable_rtree": False,
                "enable_column_metadata": False,
                "enable_json1": False,
                "enable_math_functions": False,
                "enable_unlock_notify": False,
                "omit_deprecated": True,
                "threadsafe": 2,
            },
        )
        self.requires("yaml-cpp/0.9.0")
        self.requires("date/3.0.4")
        self.requires("fmt/12.1.0")

        if self.options.with_tests:
            self.requires("gtest/1.17.0")

        if self.options.with_python:
            self.requires("pybind11/2.13.6")
