from conan import ConanFile


class BurnConan(ConanFile):
    name = "burn"
    settings = "os", "arch", "compiler", "build_type"
    generators = "CMakeDeps", "CMakeToolchain"
    default_options = {
        "boost/*:header_only": True,
    }

    def requirements(self):
        self.requires("boost/1.90.0")
        self.requires("cli11/2.6.2")
        self.requires("nlohmann_json/3.12.0")
        self.requires("fmt/12.1.0")
