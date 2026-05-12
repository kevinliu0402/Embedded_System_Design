# TCL file describing the game_peripheral component to Platform Designer.
# Mirrors lab3/vga_ball_hw.tcl but with a 32-bit Avalon-MM slave and the
# four-bit register-file address bus.

package require -exact qsys 16.1

# ---------------------------------------------------------------------
# module game_peripheral
# ---------------------------------------------------------------------
set_module_property DESCRIPTION ""
set_module_property NAME game_peripheral
set_module_property VERSION 1.0
set_module_property INTERNAL false
set_module_property OPAQUE_ADDRESS_MAP true
set_module_property AUTHOR ""
set_module_property DISPLAY_NAME "Maze Chase Game Peripheral"
set_module_property INSTANTIATE_IN_SYSTEM_MODULE true
set_module_property EDITABLE true
set_module_property REPORT_TO_TALKBACK false
set_module_property ALLOW_GREYBOX_GENERATION false
set_module_property REPORT_HIERARCHY false

set_module_assignment embeddedsw.dts.vendor "csee4840"
set_module_assignment embeddedsw.dts.name   "game_peripheral"
set_module_assignment embeddedsw.dts.group  "game"

# ---------------------------------------------------------------------
# file sets
# ---------------------------------------------------------------------
add_fileset QUARTUS_SYNTH QUARTUS_SYNTH "" ""
set_fileset_property QUARTUS_SYNTH TOP_LEVEL game_peripheral
set_fileset_property QUARTUS_SYNTH ENABLE_RELATIVE_INCLUDE_PATHS false
set_fileset_property QUARTUS_SYNTH ENABLE_FILE_OVERWRITE_MODE false
add_fileset_file game_peripheral.sv SYSTEM_VERILOG PATH game_peripheral.sv TOP_LEVEL_FILE
add_fileset_file maze.hex   OTHER PATH maze.hex
add_fileset_file evader.hex OTHER PATH evader.hex
add_fileset_file ghost0.hex OTHER PATH ghost0.hex
add_fileset_file ghost1.hex OTHER PATH ghost1.hex

# ---------------------------------------------------------------------
# clock
# ---------------------------------------------------------------------
add_interface clock clock end
set_interface_property clock clockRate 0
set_interface_property clock ENABLED true
add_interface_port clock clk clk Input 1

# ---------------------------------------------------------------------
# reset
# ---------------------------------------------------------------------
add_interface reset reset end
set_interface_property reset associatedClock     clock
set_interface_property reset synchronousEdges    DEASSERT
set_interface_property reset ENABLED             true
add_interface_port reset reset reset Input 1

# ---------------------------------------------------------------------
# avalon_slave_0  (32-bit data, 16 words = 64 bytes)
# ---------------------------------------------------------------------
add_interface avalon_slave_0 avalon end
set_interface_property avalon_slave_0 addressUnits             WORDS
set_interface_property avalon_slave_0 associatedClock          clock
set_interface_property avalon_slave_0 associatedReset          reset
set_interface_property avalon_slave_0 bitsPerSymbol            8
set_interface_property avalon_slave_0 burstOnBurstBoundariesOnly false
set_interface_property avalon_slave_0 burstcountUnits          WORDS
set_interface_property avalon_slave_0 explicitAddressSpan      0
set_interface_property avalon_slave_0 holdTime                 0
set_interface_property avalon_slave_0 linewrapBursts           false
set_interface_property avalon_slave_0 maximumPendingReadTransactions 0
set_interface_property avalon_slave_0 maximumPendingWriteTransactions 0
set_interface_property avalon_slave_0 readLatency              0
set_interface_property avalon_slave_0 readWaitTime             1
set_interface_property avalon_slave_0 setupTime                0
set_interface_property avalon_slave_0 timingUnits              Cycles
set_interface_property avalon_slave_0 writeWaitTime            0
set_interface_property avalon_slave_0 ENABLED                  true

add_interface_port avalon_slave_0 writedata   writedata   Input  32
add_interface_port avalon_slave_0 readdata    readdata    Output 32
add_interface_port avalon_slave_0 write       write       Input  1
add_interface_port avalon_slave_0 read        read        Input  1
add_interface_port avalon_slave_0 chipselect  chipselect  Input  1
add_interface_port avalon_slave_0 address     address     Input  4

set_interface_assignment avalon_slave_0 embeddedsw.configuration.isFlash             0
set_interface_assignment avalon_slave_0 embeddedsw.configuration.isMemoryDevice      0
set_interface_assignment avalon_slave_0 embeddedsw.configuration.isNonVolatileStorage 0
set_interface_assignment avalon_slave_0 embeddedsw.configuration.isPrintableDevice    0

# ---------------------------------------------------------------------
# vga conduit
# ---------------------------------------------------------------------
add_interface vga conduit end
set_interface_property vga associatedClock clock
set_interface_property vga associatedReset ""
set_interface_property vga ENABLED         true

add_interface_port vga VGA_R       r       Output 8
add_interface_port vga VGA_G       g       Output 8
add_interface_port vga VGA_B       b       Output 8
add_interface_port vga VGA_CLK     clk     Output 1
add_interface_port vga VGA_HS      hs      Output 1
add_interface_port vga VGA_VS      vs      Output 1
add_interface_port vga VGA_BLANK_n blank_n Output 1
add_interface_port vga VGA_SYNC_n  sync_n  Output 1
