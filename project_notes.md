# Project Notes - Haiku / BeTon Development Notes

This file documents critical compiler gotchas, private access workarounds, and type-safety rules found during BeTon development on Haiku.

## 1. Helper Filter Private Member Access
*   **Problem**: In `MainWindow.cpp`, helper classes like `WindowClickFilter` intercept message loops and need to inspect private members of `MainWindow` (e.g., `fLibraryManager`). Because these filters are file-local classes and not members, they cannot access `private` fields.
*   **Solution**: Declare the helper class as a `friend` in `MainWindow.h`:
    ```cpp
    private:
      friend class WindowClickFilter;
    ```

## 2. Incomplete Types & Pointer Type Casting
*   **Problem**: A subclass (like `CellTextControl`) is defined entirely in a `.cpp` file (e.g., `MediaTableView.cpp`) and only forward-declared in the header (`MediaTableView.h`). 
*   If a header-level getter method returns the forward-declared type:
    ```cpp
    CellTextControl* ActiveEditor() const { return fActiveEditor; } // Inlined in header
    ```
    Other files (like `MainWindow.cpp`) that include the header only see `CellTextControl` as an incomplete type. If they attempt to compare the return value against a base class pointer:
    ```cpp
    if (p == view->ActiveEditor()) // p is BView*
    ```
    The compiler will throw an error: `comparison between two distinct pointer types lacks a cast` because it has no knowledge that `CellTextControl` inherits from `BView`/`BTextControl`.
*   **Solution**: Return the base class pointer (`BView*`) from the getter and implement the getter in the `.cpp` file (where the subclass is fully defined and the compiler knows its inheritance hierarchy):
    *   **In Header (`MediaTableView.h`)**:
        ```cpp
        BView* ActiveEditor() const;
        ```
    *   **In Source (`MediaTableView.cpp`)**:
        ```cpp
        BView* MediaTableView::ActiveEditor() const {
          return fActiveEditor; // Compiler implicitly upcasts CellTextControl* to BView*
        }
        ```
