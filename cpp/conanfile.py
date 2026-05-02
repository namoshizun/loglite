from conan import ConanFile


class LogliteConan(ConanFile):
    name = "loglite"
    settings = "os", "arch", "compiler", "build_type"
    generators = "CMakeDeps", "CMakeToolchain"
    options = {"with_tests": [True, False]}
    default_options = {
        "boost/*:header_only": True,
        "with_tests": True,
    }

    def requirements(self):
        self.requires("boost/1.90.0")
        self.requires("cli11/2.6.2")
        self.requires("nlohmann_json/3.12.0")
        self.requires("sqlite3/3.53.0")
        self.requires("yaml-cpp/0.9.0")

        if self.options.with_tests:
            self.requires("gtest/1.17.0")
