# Simulanics Technologies - CrossBasic

This is the very first cross-platform Compiler and VM written by AI and an Agent Team, under direction of Matthew A Combatti. An AI powered plugin creator app is in the works for other developers to rapidly generate plugins for themselves and the community, using natural language. Plugins already exist and are currently under development to bring native AI and machine learning to CrossBasic. Huggingface models, Ollama, and commercial models will also be easily accessible. 🤗

[![License: CBSL-1.1](https://img.shields.io/badge/license-CBSL--1.1-blue)](https://www.crossbasic.com/license)

> 📜 **License:** This project is licensed under the [CrossBasic Business Source License 1.1 (BUSL-1.1)](./LICENSE). You may use CrossBasic to build your own projects, including commercially. You may not sell, fork, repackage, or monetize CrossBasic itself without permission. Contact [mcombatti@simulanics.com](mailto:mcombatti@simulanics.com) for commercial licensing.

# What is CrossBasic?

A compiler and virtual machine for the Xojo-esqe programming language (akin to modern Visual Basic). Compile and Run CrossBasic programs at native machine-code speeds, on any system or architecture. This includes 99.9% of devices - even the web [using emscripten]! 🙏

# CrossBasic Bytecode Compiler and Virtual Machine 🚀

Welcome to the **CrossBasic Bytecode Compiler and Virtual Machine**! This project is a bytecode compiler and virtual machine written in C++ that handles execution — including functions, classes, plugins and more - entirely cross-platform. It compiles scripts into bytecode and executes them on a cross-platform custom virtual machine, like C# and Java's design, yet is memory-safe like Go and Rust. 🤯

## Features ✨

- **Compile Standalone Executable Applications:** Use the `xcompile` tool to compile your scripts to standalone CrossBasic executable applications. 🤗
- **Cross-platform Plugin Support:** Compile and place plugins in a "libs" directory located beside the crossbasic executable. Plugins will automatically be found, loaded, and ready-to-use in your CrossBasic programs. Support for Class-object Event Handling included! (Use: AddHandler(instance.EventName, AddressOf(myFunctionName)) as you would in Xojo!)
- **Cross-platform Library Support:** Load system-level APIs using 'Declare' and use them as you would in Xojo.
- **Function Support:** Compile and execute user-defined functions and built-in ones. Overloading of functions is permitted.
- **Module Support:** Create XojoScript-style Modules.
- **Class & Instance Support:** Create classes, define methods, and instantiate objects.
- **Intrinsic Types:** Handles types like Color, Integer, Double, Boolean, Variant, Pointer and String.
- **Custom Types:** Supports creation of any Class type as a Variable. Pass or return any intrinsic or custom types between functions, subs, or plugins and event handlers.
- **Bytecode Execution:** Runs compiled bytecode on a custom cross-platform Virtual Machine (VM).
- **Debug Logging:** Step-by-step debug logs to trace lexing, parsing, compiling, and execution.
- **Intuitive Syntax:** Matches Xojo language syntax (Currently, functions require parenthesis - this is for debugging purposes. Parenthesis will be optional at a later date, as in Xojo's implementation, for interoperability and consistency.)
- **Console / Web / GUI:** Build all sorts of applications from a single code-base.
- **IDE - Create Apps and Software Anywhere:** CrossBasic offers a RAD (Rapid Application Development) IDE that's accessible from any web-accessible device. Develop at home, work, or on the go when inspiration hits! Online, offline, or run it locally! 🤗 (https://ide.crossbasic.com)
- **Embeddable Library Support:** Embed CrossBasic as a static or dynamic linked library for use with other languages.
- **Source Code Transparency:** Released under the CBSL-1.1 License.

## Getting Started 🏁

Check the Releases section for latest binaries. If your a Mac/Linux user or prefer to compile the repository yourself, continue reading below.

### Prerequisites to Build CrossBasic and Plugins from Scratch

- A C++ compiler with C++17 support (e.g. g++, clang++)
- Git (https://git-scm.com/downloads)
- libffi (https://github.com/libffi/libffi) - handles cross-platform plugin and system-level API access.
- libcurl (https://curl.se/download.html) - handles cross-platform socket and web request API's. (for use in web-accessible plugins ie. LLMConnection.dll/so/dylib)
- (Optional) Rust/Go/C#/etc - to build plugins using other languages than C++

  `Windows CrossBasicDevKit contains all necessary libraries and the build-environment for CrossBasic and it's Plugins. Windows users using the CrossBasicDevKit, will only need to install git (link above) and 64-bit Rust (https://www.rust-lang.org/tools/install).`

### Installation

1. **Clone the Repository:**

   ```bash
   git clone https://github.com/simulanics/CrossBasic
   cd CrossBasic
   ```


**Automated Building**

Use the included `build_crossbasic.bat` / `./build_crossbasic.sh` and `build_plugins.bat` / `./build_plugins.sh` scripts to automatically build and compile a release version for testing. Debugging is always available using the debug trace and profiling commandline flag (see below).

Linux users will need to chmod the build directory or individual scripts using:

`chmod -R 755 ./CrossBasic`

`chmod 755 build_xxxx.sh`

Otherwise "Permission denied" error will appear when attempting to execute the build scripts.




**Manual Building**

2. Compile the Project:

For example, using g++:

```
g++ -std=c++17 -o crossbasic crossbasic.cpp -lffi
```

With speed optimizations:

```
g++ -o crossbasic crossbasic.cpp -lffi -O3 -march=native -mtune=native -flto
```

3. Prepare Your Script:

Create a file named test.txt with your CrossBasic code. For example:

```
Function addtwonumbers(num1 As Integer, num2 As Integer) As Integer
    Return num1 + num2
End Function

Public Sub Main()
    Dim result As Integer = addtwonumbers(2, 3)
    Print(str(result))
End Sub
```

`Main() is reserved for GUI applications primarily, but is not strictly for this purpose. Xojo's implementation of XojoScript does not contain this natively, but this Sub() acts as each CrossBasic's program entry-point if supplied, and will be executed before any other top-down code.`

4. Run the Compiler on a script:

```
./crossbasic --s filename
```

Debugging 🔍

Debug trace and profile logging is enabled via the DEBUG_MODE "--d true/false" commandline flag. Set it to true or false to enable debugging:

```
./crossbasic --s filename --d true > debugtrace.log
```

This will output detailed logs for lexing, parsing, compiling, and execution.

`For optimal analysis, it is advisable to save debug trace profiles to a file, as even basic program traces can reach hundreds of megabytes due to the detailed logging of each logical step, along with any potential errors or warnings.`

Contributing 🤝

Contributions are welcome! Please feel free to open issues or submit pull requests. Your help is appreciated! 🎉

License 📄

This project is licensed under the CBSL-1.1 License. See the LICENSE file for details.

---

🤗 Happy Coding! 😄



