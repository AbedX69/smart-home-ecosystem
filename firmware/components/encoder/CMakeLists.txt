#[[
================================================================================
CMAKE BUILD CONFIGURATION FOR ENCODER COMPONENT
================================================================================

@file CMakeLists.txt
@brief Build configuration for the rotary encoder ESP-IDF component.

@details
Registers the encoder component with ESP-IDF build system, specifying:
- Source files to compile
- Include directories for headers  
- Dependencies on other ESP-IDF components

================================================================================
BEGINNER'S GUIDE: WHAT IS CMAKE AND WHY DO WE NEED THIS FILE?
================================================================================

WHAT IS CMAKE?

CMake is a "build system generator". It reads configuration files (like this one)
and generates instructions for compiling your code.

Think of it like a recipe:
    - CMakeLists.txt = The recipe (ingredients, steps)
    - CMake = The chef who reads the recipe
    - Build files = Instructions for the kitchen staff

WHY CAN'T WE JUST COMPILE DIRECTLY?

Compiling a real project involves many steps:
    1. Find all source files
    2. Find all header files
    3. Compile each source file
    4. Link them together
    5. Handle dependencies
    6. Deal with different platforms

CMake automates all of this. You just describe WHAT you want, and CMake
figures out HOW to build it.

================================================================================
WHAT IS AN ESP-IDF COMPONENT?
================================================================================

ESP-IDF organizes code into "components" - reusable building blocks.

    project/
    ├── main/                   ← Main application (special component)
    │   ├── main.cpp
    │   └── CMakeLists.txt
    │
    └── components/             ← Additional components
        └── encoder/            ← Our encoder component
            ├── encoder.cpp     ← Implementation
            ├── encoder.h       ← Header (interface)
            └── CMakeLists.txt  ← This file!

Benefits of components:
    - Reusable: Copy the folder to another project
    - Modular: Easy to understand and maintain
    - Encapsulated: Hide implementation details

================================================================================
CMAKE SYNTAX BASICS
================================================================================

CMake has its own language. Here are the basics:

    COMMANDS:
        command_name(arguments)
        
        Example: gpio_config(&io_conf)  ← This is C
                 gpio_config(io_conf)   ← This would be CMake-style
    
    STRINGS:
        Can be quoted or unquoted:
        "encoder.cpp"   ← Quoted (safe, handles spaces)
        encoder.cpp     ← Unquoted (works if no spaces)
    
    LISTS:
        Space-separated items:
        item1 item2 item3
        
        Or on multiple lines:
        item1
        item2
        item3
    
    COMMENTS:
        # Single line comment
        
        #[[ 
        Multi-line
        comment
        ]]


# ==============================================================================
# COMPONENT REGISTRATION
# ==============================================================================
#
# idf_component_register() is an ESP-IDF function that tells the build system
# about our component.
#
# It's like filling out a form:
#     "Here's my component. These are my source files. These are my headers.
#      These are the other components I need."

idf_component_register(

    # --------------------------------------------------------------------------
    # SRCS - Source Files
    # --------------------------------------------------------------------------
    #
    # List of .c or .cpp files that need to be compiled.
    #
    # WHAT GOES HERE:
    #     - Implementation files (.cpp, .c)
    #     - NOT header files (.h) - headers are included by source files
    #
    # Our component only has one source file: encoder.cpp
    # If we had more, we'd list them all:
    #     SRCS "encoder.cpp" "encoder_utils.cpp" "encoder_debug.cpp"
    #
    SRCS "encoder.cpp"
    
    
    # --------------------------------------------------------------------------
    # INCLUDE_DIRS - Header File Directories
    # --------------------------------------------------------------------------
    #
    # Where the compiler should look for header files.
    #
    # When your code writes:
    #     #include "encoder.h"
    #
    # The compiler searches these directories to find the file.
    #
    # "." means "current directory" (where this CMakeLists.txt is).
    # So encoder.h is in the same folder as this file.
    #
    # If headers were in a subfolder:
    #     INCLUDE_DIRS "include"           ← Headers in include/ folder
    #     INCLUDE_DIRS "." "include"       ← Both locations
    #
    INCLUDE_DIRS "."
    
    
    # --------------------------------------------------------------------------
    # REQUIRES - Dependencies
    # --------------------------------------------------------------------------
    #
    # Other ESP-IDF components that we need.
    #
    # WHAT HAPPENS WHEN WE LIST A DEPENDENCY:
    #     1. Its header files become available to us
    #     2. Its compiled code gets linked with ours
    #     3. It gets compiled before us (correct build order)
    #
    # OUR DEPENDENCIES:
    #
    #     driver
    #         GPIO functions: gpio_config(), gpio_get_level(),
    #         gpio_install_isr_service(), gpio_isr_handler_add()
    #
    #     esp_timer
    #         High-resolution timer: esp_timer_get_time()
    #         Used for debouncing (microsecond precision)
    #
    # COMMON ESP-IDF COMPONENTS:
    #     driver     - GPIO, SPI, I2C, UART, PWM, ADC, etc.
    #     esp_timer  - High-resolution timers
    #     esp_wifi   - WiFi functionality
    #     esp_http_client - HTTP requests
    #     nvs_flash  - Non-volatile storage (save settings)
    #     spiffs     - File system in flash
    #     freertos   - RTOS functions (usually included automatically)
    #
    REQUIRES driver esp_timer
)


#[[
================================================================================
THAT'S IT! SUMMARY
================================================================================

This simple file tells ESP-IDF:

    1. SRCS "encoder.cpp"
       → "Compile this file"
    
    2. INCLUDE_DIRS "."
       → "Headers are in the current directory"
    
    3. REQUIRES driver esp_timer
       → "We need GPIO functions and timers"

ESP-IDF handles everything else:
    - Compiling with the right flags
    - Linking with the right libraries
    - Including in the final firmware

================================================================================
ADVANCED OPTIONS (NOT USED HERE, BUT GOOD TO KNOW)
================================================================================

PRIV_REQUIRES
    Dependencies that are NOT exposed to users of this component.
    Use for internal implementation details.
    
    Example:
        REQUIRES driver            ← Public dependency
        PRIV_REQUIRES esp_timer    ← Private dependency

EMBED_FILES
    Binary files to embed in firmware (images, data files, etc.)
    
    Example:
        EMBED_FILES "logo.png" "config.bin"

EMBED_TXTFILES  
    Text files to embed (automatically null-terminated)
    
    Example:
        EMBED_TXTFILES "version.txt" "help.txt"

WHOLE_ARCHIVE
    Force include ALL symbols from a library.
    Used for plugin systems or when symbols are only referenced at runtime.
    
    Example:
        WHOLE_ARCHIVE

================================================================================
]]
