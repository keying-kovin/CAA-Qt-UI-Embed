# Embedding Qt UI into CAA Dialogs

As we all know, the native dialog designer in CAA is quite rudimentary, making it nearly impossible to implement complex and refined interfaces. Qt, however, perfectly compensates for these shortcomings. Hence, the idea arose to embed Qt UI into CAA dialogs.

## Version Compatibility

There is a prerequisite environment. If you are developing on 3DE, it should come with a `Qt5Plugins` folder (or `Qt6` in newer versions). First, open the properties of any `.dll` file inside that folder, go to the **Details** tab, and check the product version or file version (mine is `5.9.0.0`). Then download the corresponding Qt version from the official Qt website. The compiler should match your Visual Studio version (e.g., 2015, 2017, ...). Once the download is complete, the basic environment setup is done.
*(I encountered many pitfalls while following others' tutorials—for example, 3DE ships with `qmainwindow.dll`, and mismatched Qt versions caused failures in initializing `QApplication`, etc.)*

## File Structure

Initially, I placed both CAA and Qt files under the same module, which worked. However, if you want to encapsulate Qt so that other modules can also use the Qt UI, you need to separate them into distinct packages.

All Qt header files are placed in `publicinterfaces`; otherwise, the compiler cannot find them.

## Linking the Qt Environment

In the `imakefile`, I added `LOCAL_LDFLAGS`. The first `.lib` file is generated after compiling `qt_src`, with the path specified in `build_qt.bat` (default is `Object/win_b64` under the same module). The next `libpath` points to the directory containing the Qt5 `.lib` files (e.g., `xx\xx\msvc2015_64\bin`), which come from your Qt installation. `user32.lib` is a system file.

In the bridge file, I also added `addLibraryPath` to include the 3DE plugin folder location. When I consulted Codex, it suggested that 3DE would automatically find that path, but after commenting it out, errors occurred. Therefore, I am unsure whether this is strictly necessary, but I kept it in.

Additionally, I passed a JSON file as the dataset for testing the tree table. I admit this encapsulation is not ideal, but due to limited time and expertise, I left it as is—modifying it properly would require significant effort.

------

Finally, both the Qt and CAA files are freely adjustable. This is merely one approach and a proof of concept. Feedback and further exploration are welcome!