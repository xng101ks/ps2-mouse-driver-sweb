# PS/2 Mouse Driver for SWEB (x86_64)

An interrupt-driven x86_64 PS/2 mouse driver implemented in C++ for **SWEB**, an educational OS developed at Graz University of Technology (TU Graz).

![Demo](./demo.gif)

---

## Background & Project Context

This driver was developed independently by the author as an **extra-credit assignment** while taking the **Operating Systems** course at **Graz University of Technology (TU Graz)**.

**SWEB** (*Schon wieder ein Betriebssystem*) is an educational operating system framework developed at TU Graz (Institute of Information Security / IAIK) to teach core OS concepts such as low-level hardware control, interrupt handling, thread scheduling, and memory management.  
https://github.com/isec-tugraz/sweb

### Why Only Driver Files Are Published
To comply with university policies and academic integrity guidelines regarding the redistribution of SWEB source code and course materials, the entire SWEB operating system codebase cannot be re-uploaded or published publicly.

For this reason, this repository hosts **only the original driver files** developed for the mouse driver:
* `arch/common/include/MouseManager.h`
* `arch/x86/common/source/MouseManager.cpp`

To enable students, instructors, or anyone with access to SWEB to build and run this driver, a complete **integration tutorial** is provided below detailing the required kernel hooks, interrupt setup, and console modifications.

---

## Features

- **Interrupt-Driven I/O**: Operates via **IRQ 12** on the x86 8259 PIC and the Interrupt Descriptor Table (IDT).
- **Terminal Cursor Rendering**: Tracks bounded absolute coordinates on the SWEB terminal screen, saves the character under the cursor (`^`), and restores the underlying text when the mouse moves.
- **Kernel Debugging**: Integrates with SWEB's `debug(...)` facility and ANSI color logging.

---

## Repository Structure

```text
.
├── README.md
├── demo.gif
└── arch/
    ├── common/
    │   └── include/
    │       └── MouseManager.h
    └── x86/
        └── common/
            └── source/
                └── MouseManager.cpp
```

---

## Integration Tutorial: Adding the Mouse Driver to SWEB

Follow the steps below to integrate the mouse driver into an existing SWEB working tree.

### Step 1: Copy Driver Files into SWEB
Copy the driver files from this repository directly into your SWEB source tree:
- Copy `arch/common/include/MouseManager.h` to `arch/common/include/`
- Copy `arch/x86/common/source/MouseManager.cpp` to `arch/x86/common/source/`

---

### Step 2: Set Up IRQ 12 & Interrupt Handlers

#### 2.1 Declare Enable and Disable Methods
Add the following public static methods to the `ArchInterrupts` class in `arch/common/include/ArchInterrupts.h`:

```cpp
    static void enableMouse();
    static void disableMouse();
```

#### 2.2 Implement Enable and Disable Methods
Add the method definitions to `arch/x86/64/source/ArchInterrupts.cpp`:

```cpp
    void ArchInterrupts::enableMouse()
    {
        // Interrupt 12 is the PS/2 mouse interrupt on x86
        // Note: we also enable this interrupt in arch_interrupts.S and add it
        // to the IDT handler table
        enableIRQ(12);
    }

    void ArchInterrupts::disableMouse()
    {
        disableIRQ(12);
    }
```

#### 2.3 Include Driver Header and Define IRQ Handler
In `arch/x86/64/source/InterruptUtils.cpp`, add the header include:

```cpp
    #include "MouseManager.h"
```

And define the IRQ 12 handler:

```cpp
    extern "C" void arch_irqHandler_12();
    extern "C" void irqHandler_12()
    {
        debug(MOUSE_DRIVER, "in irq handler 12 now...\n");
        //kprintfd( "IRQ 12 called\n" );
        ++outstanding_EOIs;
        MouseManager::instance()->serviceIRQ();
        ArchInterrupts::EndOfInterrupt(12);
    }
```

#### 2.4 Register Interrupt in the Assembly Entry Layer
In `arch/x86/64/source/arch_interrupts.S`, update the `.irp` list to include interrupt `12`:

```diff
-    .irp num,0,1,3,4,6,9,11,14,15,65
+    .irp num,0,1,3,4,6,9,11,12,14,15,65
```

#### 2.5 Add Handler to the IDT Handler Table
In `arch/x86/common/include/ErrorHandlers.h`, add the handler macro to the `InterruptUtils::handlers[]` struct array (ensuring table formatting remains intact):

```cpp
    IRQHANDLER(12)
```

---

### Step 3: Add Functions to Get and Set Terminal Characters

To render the mouse cursor and preserve the underlying terminal character, `Terminal` must expose row/column accessors and a getter method.

#### 3.1 Update Method Visibility and Add `getCharacter` Declaration
Make the following method declarations public in `common/include/console/Terminal.h`:

```cpp
    uint32 setCharacter(uint32 row, uint32 column, uint8 character);
    uint32 getNumRows() const;
    uint32 getNumColumns() const;
```

And add the new declaration:

```cpp
    uint8 getCharacter(uint32 row, uint32 column);
```

#### 3.2 Implement `getCharacter`
Add the method implementation to `common/source/console/Terminal.cpp`:

```cpp
    uint8 Terminal::getCharacter(uint32 row, uint32 column)
    {
        return characters_[column + row * num_columns_];
    }
```

---

### Step 4: Instantiate MouseManager and Enable Interrupts

In `common/source/kernel/main.cpp`, include the driver header:

```cpp
    #include "MouseManager.h"
```

Then instantiate the singleton and enable interrupts during kernel initialization (e.g. after enabling the keyboard, but before adding kernel threads):

```cpp
    // After enabling keyboard, but before adding kernel threads
    MouseManager::instance();
    ArchInterrupts::enableMouse();
```

---

### Step 5: Define Debug Output Color

To colorize driver logs in the kernel debug console, add the following definition to `common/include/console/debug.h`:

```cpp
    const size_t MOUSE_DRIVER       = Ansi_Yellow;// | OUTPUT_ENABLED;
```

---

## Acknowledgments & Disclaimer

- **SWEB** was created by the Institute of Information Security (ISEC, formerly IAIK) at Graz University of Technology (TU Graz) for educational purposes.
- This repository is an independent academic project developed individually by the author for course extra credits and is not officially affiliated with or endorsed by TU Graz.
