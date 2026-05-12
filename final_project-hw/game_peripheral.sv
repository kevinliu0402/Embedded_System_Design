/*
 * Maze Chase Game - Custom Avalon-MM Peripheral
 *
 * Top-level Verilog module instantiated in soc_system_top.
 * Generates the 640x480 VGA signal for the maze game and exposes a
 * 32-bit Avalon-MM slave register file to the HPS (Lightweight bridge).
 *
 * Style follows Lab 3 (vga_ball.sv): one .sv file holds the top module
 * and every sub-module the design needs.
 *
 * Register map (byte offsets, 32-bit words)
 * =====================================================================
 *  Off  Word  Name            Dir  Updated by  Description
 *  0x00  0   EVADER_POS       R/W  HPS         {row[4:0], col[4:0]}
 *  0x04  1   GHOST0_POS       R/W  HPS         {row[4:0], col[4:0]}
 *  0x08  2   GHOST1_POS       R/W  HPS         {row[4:0], col[4:0]}
 *  0x0C  3   TILE_STATUS      R    FPGA        [5:0] = activated flags
 *  0x10  4   TILE0_TIME       R    FPGA        [13:0] = 0..600 frames
 *  0x14  5   TILE1_TIME       R    FPGA
 *  0x18  6   TILE2_TIME       R    FPGA
 *  0x1C  7   TILE3_TIME       R    FPGA
 *  0x20  8   TILE4_TIME       R    FPGA
 *  0x24  9   TILE5_TIME       R    FPGA
 *  0x28 10   GAME_STATUS      R/W  HPS         [0]=gate_open [1]=ev_win
 *                                              [2]=gh_win    [3]=reset
 *  0x2C 11   FRAME_SYNC       R    FPGA        [15:0]=frame# [16]=vsync
 *  0x30-0x3C 12-15 RESERVED
 * =====================================================================
 */

module game_peripheral(
    input  logic        clk,            // 50 MHz system clock
    input  logic        reset,          // active-high synchronous reset

    // Avalon-MM slave (Lightweight HPS-to-FPGA bridge)
    input  logic [3:0]  address,        // word address (16 regs)
    input  logic        chipselect,
    input  logic        write,
    input  logic        read,
    input  logic [31:0] writedata,
    output logic [31:0] readdata,

    // VGA outputs (direct to DE1-SoC ADV7123)
    output logic [7:0]  VGA_R, VGA_G, VGA_B,
    output logic        VGA_CLK,
    output logic        VGA_HS,
    output logic        VGA_VS,
    output logic        VGA_BLANK_n,
    output logic        VGA_SYNC_n
);

    // -----------------------------------------------------------------
    // Internal nets
    // -----------------------------------------------------------------
    logic [10:0] hcount;                 // hcount[10:1] is pixel column
    logic [9:0]  vcount;
    logic        vsync_pulse;            // pixel_clk-domain 1-cycle pulse
    logic        vsync_tick;             // clk-domain CDC-safe pulse
    logic        pixel_clk;

    // Register file outputs
    logic [4:0]  evader_col, evader_row;
    logic [4:0]  ghost0_col, ghost0_row;
    logic [4:0]  ghost1_col, ghost1_row;
    logic [3:0]  game_status_q;          // {reset, gh_win, ev_win, gate_open}

    // Tile timers
    logic [5:0]  tile_activated;
    logic [13:0] tile_count [0:5];

    // Frame counter
    logic [15:0] frame_count;

    // Maze / sprite ROM outputs
    logic [9:0]  maze_addr;
    logic [2:0]  maze_cell;
    logic [7:0]  sprite_addr;
    logic [2:0]  evader_pixel, ghost0_pixel, ghost1_pixel;

    // -----------------------------------------------------------------
    // VGA timing -> hcount/vcount + VGA control signals
    // -----------------------------------------------------------------
    vga_counters u_counters (
        .clk50       (clk),
        .reset       (reset),
        .hcount      (hcount),
        .vcount      (vcount),
        .VGA_CLK     (VGA_CLK),
        .VGA_HS      (VGA_HS),
        .VGA_VS      (VGA_VS),
        .VGA_BLANK_n (VGA_BLANK_n),
        .VGA_SYNC_n  (VGA_SYNC_n),
        .vsync_pulse (vsync_pulse)
    );

    // pixel_clk derives from hcount[0] -> 25 MHz strobe; ROMs use clk and
    // sample every other cycle.  We use clk as the ROM clock and gate the
    // address with hcount[0] so the registered output appears on the right
    // cycle.  Simpler: just use clk; address changes once per pixel pair.
    assign pixel_clk = clk;  // unused but kept for clarity

    // -----------------------------------------------------------------
    // CDC: bring vsync from pixel-clock counter to system clock
    // -----------------------------------------------------------------
    vsync_cdc u_cdc (
        .clk         (clk),
        .reset       (reset),
        .vsync_pulse (vsync_pulse),
        .vsync_tick  (vsync_tick)
    );

    // Maintain a 16-bit frame counter incremented on every vsync_tick
    always_ff @(posedge clk) begin
        if (reset)
            frame_count <= 16'd0;
        else if (vsync_tick)
            frame_count <= frame_count + 16'd1;
    end

    // -----------------------------------------------------------------
    // Avalon-MM register file
    // -----------------------------------------------------------------
    // Bit layout for *_POS regs: writedata[4:0]=col, writedata[9:5]=row
    logic do_write;
    logic [9:0] evader_pos_q, ghost0_pos_q, ghost1_pos_q;

    assign do_write = chipselect && write;
    assign evader_col = evader_pos_q[4:0];
    assign evader_row = evader_pos_q[9:5];
    assign ghost0_col = ghost0_pos_q[4:0];
    assign ghost0_row = ghost0_pos_q[9:5];
    assign ghost1_col = ghost1_pos_q[4:0];
    assign ghost1_row = ghost1_pos_q[9:5];

    // GAME_RESET is self-clearing: hardware clears bit[3] on the cycle it
    // sees it asserted.  Win/loss/gate flags are sticky until reset.
    always_ff @(posedge clk) begin
        if (reset) begin
            // Initial entity positions (must all be passable cells)
            evader_pos_q  <= {5'd10, 5'd15};  // (col=15, row=10) center
            ghost0_pos_q  <= {5'd2,  5'd1 };  // (col=1,  row=2)
            ghost1_pos_q  <= {5'd2,  5'd28};  // (col=28, row=2)
            game_status_q <= 4'b0000;
        end else begin
            if (game_status_q[3]) begin
                // GAME_RESET asserted: clear everything
                evader_pos_q  <= {5'd10, 5'd15};
                ghost0_pos_q  <= {5'd2,  5'd1 };
                ghost1_pos_q  <= {5'd2,  5'd28};
                game_status_q <= 4'b0000;
            end else if (do_write) begin
                case (address)
                    4'd0 : evader_pos_q  <= writedata[9:0];
                    4'd1 : ghost0_pos_q  <= writedata[9:0];
                    4'd2 : ghost1_pos_q  <= writedata[9:0];
                    4'd10: game_status_q <= writedata[3:0];
                    default: /* read-only or reserved */;
                endcase
            end
        end
    end

    // Read-mux
    always_comb begin
        case (address)
            4'd0 : readdata = {22'd0, evader_pos_q};
            4'd1 : readdata = {22'd0, ghost0_pos_q};
            4'd2 : readdata = {22'd0, ghost1_pos_q};
            4'd3 : readdata = {26'd0, tile_activated};
            4'd4 : readdata = {18'd0, tile_count[0]};
            4'd5 : readdata = {18'd0, tile_count[1]};
            4'd6 : readdata = {18'd0, tile_count[2]};
            4'd7 : readdata = {18'd0, tile_count[3]};
            4'd8 : readdata = {18'd0, tile_count[4]};
            4'd9 : readdata = {18'd0, tile_count[5]};
            4'd10: readdata = {28'd0, game_status_q};
            4'd11: readdata = {15'd0, vsync_tick, frame_count};
            default: readdata = 32'd0;
        endcase
    end

    // -----------------------------------------------------------------
    // Tile timers (6 instances at hardcoded grid positions)
    // -----------------------------------------------------------------
    // Hardcoded special-tile positions from the design document
    localparam logic [4:0] T0_COL = 5'd3,  T0_ROW = 5'd2;
    localparam logic [4:0] T1_COL = 5'd26, T1_ROW = 5'd2;
    localparam logic [4:0] T2_COL = 5'd3,  T2_ROW = 5'd10;
    localparam logic [4:0] T3_COL = 5'd26, T3_ROW = 5'd10;
    localparam logic [4:0] T4_COL = 5'd14, T4_ROW = 5'd17;
    localparam logic [4:0] T5_COL = 5'd17, T5_ROW = 5'd17;
    localparam logic [4:0] GATE_COL = 5'd15, GATE_ROW = 5'd1;

    // Single combined reset: external reset OR self-clearing GAME_RESET
    logic reset_int;
    assign reset_int = reset | game_status_q[3];

    tile_timer #(.TILE_COL(T0_COL), .TILE_ROW(T0_ROW)) u_t0 (
        .clk(clk), .reset(reset_int), .vsync_tick(vsync_tick),
        .evader_col(evader_col), .evader_row(evader_row),
        .count(tile_count[0]), .activated(tile_activated[0]));
    tile_timer #(.TILE_COL(T1_COL), .TILE_ROW(T1_ROW)) u_t1 (
        .clk(clk), .reset(reset_int), .vsync_tick(vsync_tick),
        .evader_col(evader_col), .evader_row(evader_row),
        .count(tile_count[1]), .activated(tile_activated[1]));
    tile_timer #(.TILE_COL(T2_COL), .TILE_ROW(T2_ROW)) u_t2 (
        .clk(clk), .reset(reset_int), .vsync_tick(vsync_tick),
        .evader_col(evader_col), .evader_row(evader_row),
        .count(tile_count[2]), .activated(tile_activated[2]));
    tile_timer #(.TILE_COL(T3_COL), .TILE_ROW(T3_ROW)) u_t3 (
        .clk(clk), .reset(reset_int), .vsync_tick(vsync_tick),
        .evader_col(evader_col), .evader_row(evader_row),
        .count(tile_count[3]), .activated(tile_activated[3]));
    tile_timer #(.TILE_COL(T4_COL), .TILE_ROW(T4_ROW)) u_t4 (
        .clk(clk), .reset(reset_int), .vsync_tick(vsync_tick),
        .evader_col(evader_col), .evader_row(evader_row),
        .count(tile_count[4]), .activated(tile_activated[4]));
    tile_timer #(.TILE_COL(T5_COL), .TILE_ROW(T5_ROW)) u_t5 (
        .clk(clk), .reset(reset_int), .vsync_tick(vsync_tick),
        .evader_col(evader_col), .evader_row(evader_row),
        .count(tile_count[5]), .activated(tile_activated[5]));

    // -----------------------------------------------------------------
    // Maze rendering coordinates (pixel-clock domain operates on clk too,
    // since vga_counters uses clk50 to produce 25 MHz strobe via hcount[0])
    // -----------------------------------------------------------------
    logic [9:0] pixel_col, pixel_row;
    assign pixel_col = hcount[10:1];           // 0..639
    assign pixel_row = vcount;                 // 0..479

    logic in_maze_x, in_maze_y, in_maze;
    assign in_maze_x = (pixel_col >= 10'd80) && (pixel_col < 10'd560);
    assign in_maze_y = (pixel_row >= 10'd80) && (pixel_row < 10'd400);
    assign in_maze   = in_maze_x && in_maze_y;

    logic [9:0]  maze_x, maze_y;
    assign maze_x = pixel_col - 10'd80;        // 0..479
    assign maze_y = pixel_row - 10'd80;        // 0..319

    logic [4:0]  tile_col, tile_row;
    logic [3:0]  px_in_tile, py_in_tile;
    assign tile_col   = maze_x[8:4];
    assign tile_row   = maze_y[8:4];
    assign px_in_tile = maze_x[3:0];
    assign py_in_tile = maze_y[3:0];

    // Tile-index address: row*30 + col
    // Multiplication by 30 = (row<<5) - (row<<1)
    assign maze_addr   = ({5'd0, tile_row} * 10'd30) + {5'd0, tile_col};
    assign sprite_addr = {py_in_tile, px_in_tile};

    // -----------------------------------------------------------------
    // ROMs
    // -----------------------------------------------------------------
    maze_rom u_maze_rom (
        .clk       (clk),
        .addr      (maze_addr),
        .cell_type (maze_cell)
    );

    sprite_rom #(.INIT_FILE("evader.hex")) u_evader_rom (
        .clk(clk), .addr(sprite_addr), .pixel_color(evader_pixel));
    sprite_rom #(.INIT_FILE("ghost0.hex")) u_ghost0_rom (
        .clk(clk), .addr(sprite_addr), .pixel_color(ghost0_pixel));
    sprite_rom #(.INIT_FILE("ghost1.hex")) u_ghost1_rom (
        .clk(clk), .addr(sprite_addr), .pixel_color(ghost1_pixel));

    // -----------------------------------------------------------------
    // Compositor + palette
    // -----------------------------------------------------------------
    compositor u_compositor (
        .VGA_BLANK_n     (VGA_BLANK_n),
        .in_maze         (in_maze),
        .tile_col        (tile_col),
        .tile_row        (tile_row),
        .maze_cell       (maze_cell),
        .evader_pixel    (evader_pixel),
        .ghost0_pixel    (ghost0_pixel),
        .ghost1_pixel    (ghost1_pixel),
        .evader_col      (evader_col),
        .evader_row      (evader_row),
        .ghost0_col      (ghost0_col),
        .ghost0_row      (ghost0_row),
        .ghost1_col      (ghost1_col),
        .ghost1_row      (ghost1_row),
        .tile_activated  (tile_activated),
        .gate_open       (game_status_q[0]),
        .evader_win      (game_status_q[1]),
        .ghost_win       (game_status_q[2]),
        .gate_col        (GATE_COL),
        .gate_row        (GATE_ROW),
        .VGA_R           (VGA_R),
        .VGA_G           (VGA_G),
        .VGA_B           (VGA_B)
    );

endmodule


// =====================================================================
// vga_counters - 640x480 timing for a 50 MHz clock (copied from Lab 3)
// =====================================================================
module vga_counters(
    input  logic        clk50, reset,
    output logic [10:0] hcount,
    output logic [9:0]  vcount,
    output logic        VGA_CLK, VGA_HS, VGA_VS, VGA_BLANK_n, VGA_SYNC_n,
    output logic        vsync_pulse
);

    parameter HACTIVE      = 11'd1280,
              HFRONT_PORCH = 11'd32,
              HSYNC        = 11'd192,
              HBACK_PORCH  = 11'd96,
              HTOTAL       = HACTIVE + HFRONT_PORCH + HSYNC + HBACK_PORCH;

    parameter VACTIVE      = 10'd480,
              VFRONT_PORCH = 10'd10,
              VSYNC        = 10'd2,
              VBACK_PORCH  = 10'd33,
              VTOTAL       = VACTIVE + VFRONT_PORCH + VSYNC + VBACK_PORCH;

    logic endOfLine, endOfField;

    always_ff @(posedge clk50 or posedge reset)
        if (reset)          hcount <= 0;
        else if (endOfLine) hcount <= 0;
        else                hcount <= hcount + 11'd1;

    assign endOfLine = hcount == HTOTAL - 1;

    always_ff @(posedge clk50 or posedge reset)
        if (reset)          vcount <= 0;
        else if (endOfLine)
            if (endOfField) vcount <= 0;
            else            vcount <= vcount + 10'd1;

    assign endOfField = vcount == VTOTAL - 1;

    // Single-cycle pulse at start of frame (used for vsync_cdc)
    assign vsync_pulse = endOfLine && endOfField;

    assign VGA_HS = !((hcount[10:8] == 3'b101) & !(hcount[7:5] == 3'b111));
    assign VGA_VS = !(vcount[9:1] == (VACTIVE + VFRONT_PORCH) / 2);
    assign VGA_SYNC_n  = 1'b0;
    assign VGA_BLANK_n = !(hcount[10] & (hcount[9] | hcount[8])) &
                         !(vcount[9] | (vcount[8:5] == 4'b1111));
    assign VGA_CLK = hcount[0];
endmodule


// =====================================================================
// vsync_cdc - 2-flop synchroniser + rising-edge detect.  The source pulse
// comes from the pixel-clock-domain vga_counters; we want a single-cycle
// strobe in the clk domain.
// =====================================================================
module vsync_cdc(
    input  logic clk,
    input  logic reset,
    input  logic vsync_pulse,
    output logic vsync_tick
);
    logic meta_q, sync_q, prev_q;
    always_ff @(posedge clk) begin
        if (reset) begin
            meta_q <= 1'b0;
            sync_q <= 1'b0;
            prev_q <= 1'b0;
        end else begin
            meta_q <= vsync_pulse;
            sync_q <= meta_q;
            prev_q <= sync_q;
        end
    end
    assign vsync_tick = sync_q & ~prev_q;
endmodule


// =====================================================================
// tile_timer - 14-bit saturating counter, increments while Evader is on
// the hardcoded (TILE_COL, TILE_ROW) tile and we receive a vsync_tick.
// Activated flag is combinational (count >= 600).
// =====================================================================
module tile_timer #(
    parameter logic [4:0] TILE_COL = 5'd0,
    parameter logic [4:0] TILE_ROW = 5'd0
) (
    input  logic        clk,
    input  logic        reset,
    input  logic        vsync_tick,
    input  logic [4:0]  evader_col,
    input  logic [4:0]  evader_row,
    output logic [13:0] count,
    output logic        activated
);
    logic on_tile;
    assign on_tile   = (evader_col == TILE_COL) && (evader_row == TILE_ROW);
    assign activated = (count >= 14'd600);

    always_ff @(posedge clk) begin
        if (reset)
            count <= 14'd0;
        else if (vsync_tick && on_tile && !activated)
            count <= count + 14'd1;
    end
endmodule


// =====================================================================
// maze_rom - 600 x 3-bit synchronous ROM.  Cell encoding:
//   3'd0 = WALL, 3'd1 = PATH, 3'd2 = SPECIAL, 3'd3 = GATE.
// Initialised from maze.hex generated by gen_mif.py.
// =====================================================================
module maze_rom(
    input  logic       clk,
    input  logic [9:0] addr,
    output logic [2:0] cell_type
);
    (* ramstyle = "M10K" *) logic [2:0] mem [0:599];
    initial $readmemh("maze.hex", mem);

    always_ff @(posedge clk)
        cell_type <= mem[addr];
endmodule


// =====================================================================
// sprite_rom - 256 x 3-bit synchronous ROM (16x16 sprite).
//   Index 3'd0 is treated as transparent by the compositor.
// =====================================================================
module sprite_rom #(parameter INIT_FILE = "evader.hex") (
    input  logic       clk,
    input  logic [7:0] addr,
    output logic [2:0] pixel_color
);
    (* ramstyle = "M10K" *) logic [2:0] mem [0:255];
    initial $readmemh(INIT_FILE, mem);

    always_ff @(posedge clk)
        pixel_color <= mem[addr];
endmodule


// =====================================================================
// compositor - chooses the final RGB value for the current pixel.
//
// Priority order at a given pixel:
//   1.  If !VGA_BLANK_n  -> black
//   2.  If game_result   -> full-screen overlay (green=evader win,
//                                                red  =ghost  win)
//   3.  If !in_maze      -> black border
//   4.  Sprite layer:  evader > ghost0 > ghost1 (non-zero pixel wins)
//   5.  Tile layer:    cell_type drives a palette colour, modulated by
//                      tile_activated[] and gate_open.
// =====================================================================
module compositor(
    input  logic        VGA_BLANK_n,
    input  logic        in_maze,
    input  logic [4:0]  tile_col, tile_row,
    input  logic [2:0]  maze_cell,
    input  logic [2:0]  evader_pixel,
    input  logic [2:0]  ghost0_pixel,
    input  logic [2:0]  ghost1_pixel,
    input  logic [4:0]  evader_col, evader_row,
    input  logic [4:0]  ghost0_col, ghost0_row,
    input  logic [4:0]  ghost1_col, ghost1_row,
    input  logic [5:0]  tile_activated,
    input  logic        gate_open,
    input  logic        evader_win,
    input  logic        ghost_win,
    input  logic [4:0]  gate_col, gate_row,
    output logic [7:0]  VGA_R, VGA_G, VGA_B
);
    // ------ identify which tile (if any) we are currently on ---------
    logic on_evader, on_ghost0, on_ghost1;
    assign on_evader = (tile_col == evader_col) && (tile_row == evader_row);
    assign on_ghost0 = (tile_col == ghost0_col) && (tile_row == ghost0_row);
    assign on_ghost1 = (tile_col == ghost1_col) && (tile_row == ghost1_row);

    // ------ identify if current tile is one of the 6 special tiles --
    logic        is_special_tile;
    logic [2:0]  special_idx;
    always_comb begin
        is_special_tile = 1'b0;
        special_idx     = 3'd0;
        case ({tile_col, tile_row})
            {5'd3 , 5'd2 }: begin is_special_tile = 1'b1; special_idx = 3'd0; end
            {5'd26, 5'd2 }: begin is_special_tile = 1'b1; special_idx = 3'd1; end
            {5'd3 , 5'd10}: begin is_special_tile = 1'b1; special_idx = 3'd2; end
            {5'd26, 5'd10}: begin is_special_tile = 1'b1; special_idx = 3'd3; end
            {5'd14, 5'd17}: begin is_special_tile = 1'b1; special_idx = 3'd4; end
            {5'd17, 5'd17}: begin is_special_tile = 1'b1; special_idx = 3'd5; end
            default: ;
        endcase
    end
    logic this_tile_active;
    assign this_tile_active = is_special_tile && tile_activated[special_idx];
    logic is_gate_tile;
    assign is_gate_tile = (tile_col == gate_col) && (tile_row == gate_row);

    // ------ tile background colour (from maze_cell + activation) ----
    logic [23:0] tile_rgb;
    always_comb begin
        unique case (maze_cell)
            3'd0:    tile_rgb = 24'h202050;                 // WALL  : dark blue
            3'd1:    tile_rgb = 24'h000000;                 // PATH  : black
            3'd2:    tile_rgb = this_tile_active ? 24'h60E060   // active green
                                                 : 24'h605030;  // dim yellow
            3'd3:    tile_rgb = gate_open       ? 24'h60E060   // gate open
                                                 : 24'h606060;  // grey
            default: tile_rgb = 24'h000000;
        endcase
    end

    // ------ sprite colour palette (3-bit index)  --------------------
    // 0 = transparent (handled by compositor mux)
    // 1..7 = colours.  Each sprite ROM has its own meaning per index but
    // we share a single palette here.  Evader uses warm colours; ghost0
    // bluish; ghost1 reddish.  The colour wheels are encoded in the .hex
    // files; we just need a way to turn a 3-bit value into RGB for each
    // entity.  To keep the compositor simple we use the same palette and
    // let the .hex contents do the differentiation.
    function automatic [23:0] palette(input logic [2:0] idx);
        case (idx)
            3'd0: palette = 24'h000000;  // transparent (never selected)
            3'd1: palette = 24'hFFFFFF;  // white
            3'd2: palette = 24'hFFD030;  // gold (evader body)
            3'd3: palette = 24'hC04020;  // red-brown (evader outline)
            3'd4: palette = 24'h4080FF;  // blue   (ghost0 body)
            3'd5: palette = 24'h2040A0;  // dk blue (ghost0 outline)
            3'd6: palette = 24'hFF6060;  // red    (ghost1 body)
            3'd7: palette = 24'hA02020;  // dk red (ghost1 outline)
            default: palette = 24'h000000;
        endcase
    endfunction

    // ------ pick which entity (if any) covers this pixel -----------
    logic [2:0] sprite_pix;
    always_comb begin
        sprite_pix = 3'd0;
        if (on_evader && evader_pixel != 3'd0)
            sprite_pix = evader_pixel;
        else if (on_ghost0 && ghost0_pixel != 3'd0)
            sprite_pix = ghost0_pixel;
        else if (on_ghost1 && ghost1_pixel != 3'd0)
            sprite_pix = ghost1_pixel;
    end

    logic [23:0] pixel_rgb;
    always_comb begin
        if (!VGA_BLANK_n) begin
            pixel_rgb = 24'h000000;
        end else if (evader_win) begin
            pixel_rgb = 24'h00B000;       // green overlay
        end else if (ghost_win) begin
            pixel_rgb = 24'hB00000;       // red overlay
        end else if (!in_maze) begin
            pixel_rgb = 24'h000000;       // black border
        end else if (sprite_pix != 3'd0) begin
            pixel_rgb = palette(sprite_pix);
        end else begin
            pixel_rgb = tile_rgb;
        end
    end

    assign {VGA_R, VGA_G, VGA_B} = pixel_rgb;
endmodule
