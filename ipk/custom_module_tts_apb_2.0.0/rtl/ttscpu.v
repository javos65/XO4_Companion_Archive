// TTSCPU: small multi-cycle processor that runs the TTS firmware (see tools/asm.py, tools/isasim.py).
// 32-bit data, 16 registers, 3 clocks per instruction (fetch, decode, execute) plus 16 clocks for MUL/MULQ.
// Must match tools/isasim.py bit for bit.
module ttscpu (
    input  wire        clk,
    input  wire        rst,
    input  wire        start,          // one-clock pulse: restart the program at address 0
    output reg         done,           // high while halted
    output reg  [6:0]  lb_addr,        // line buffer read address (OUT port 2)
    input  wire [7:0]  lb_data,        // line buffer data (IN port 2)
    input  wire [7:0]  lb_len,         // line length (IN port 3)
    output reg         smp_we,         // push one PWM sample (OUT port 1)
    output reg  [19:0] smp_data,
    input  wire        smp_full        // FIFO full: OUT port 1 stalls
);
    localparam [5:0]
        OP_NOP = 6'h00, OP_LI = 6'h01, OP_LUI = 6'h02, OP_ADD = 6'h03, OP_ADDI = 6'h04,
        OP_SUB = 6'h05, OP_AND = 6'h06, OP_ANDI = 6'h07, OP_OR = 6'h08, OP_ORI = 6'h09,
        OP_XOR = 6'h0A, OP_SLL = 6'h0B, OP_SRL = 6'h0C, OP_SRA = 6'h0D, OP_MUL = 6'h10,
        OP_MULQ = 6'h11, OP_SLT = 6'h12, OP_SLTI = 6'h13, OP_LDB = 6'h18, OP_LDW = 6'h19,
        OP_STW = 6'h1A, OP_LDT = 6'h1B, OP_BEQ = 6'h20, OP_BNE = 6'h21, OP_BLT = 6'h22,
        OP_BGE = 6'h23, OP_JMP = 6'h24, OP_CALL = 6'h25, OP_RET = 6'h26, OP_IN = 6'h28,
        OP_OUT = 6'h29, OP_HALT = 6'h3F;

    localparam [2:0] S_HALT = 3'd0, S_F = 3'd1, S_D = 3'd2, S_M = 3'd3, S_E = 3'd4;

    reg [2:0]  state;
    reg [10:0] pc;
    reg [31:0] regs [0:15];

    // ---- instruction fetch (synchronous ROM, output stays valid while pc is stable) ----
    wire [31:0] ir;
    tts_prog_rom u_prog (.clk(clk), .a(pc), .q(ir));

    wire [5:0]  op   = ir[31:26];
    wire [3:0]  rd   = ir[25:22];
    wire [3:0]  ra   = ir[21:18];
    wire [17:0] imm  = ir[17:0];
    wire [3:0]  rb   = imm[3:0];
    wire signed [31:0] simm = {{14{imm[17]}}, imm};

    wire signed [31:0] A  = regs[ra];
    wire signed [31:0] B  = regs[rb];
    wire signed [31:0] Bd = regs[rd];

    // ---- data memories (read data registered in decode state) ----
    wire [31:0] maddr = A + simm;
    wire [7:0]  brom_q;
    wire [15:0] trom_q;
    reg  [31:0] ram [0:1023];
    reg  [31:0] ram_q;
    integer     ri;
    initial for (ri = 0; ri < 1024; ri = ri + 1) ram[ri] = 32'd0;

    tts_brom u_brom (.clk(clk), .a(maddr[12:0]), .q(brom_q));
    tts_trom u_trom (.clk(clk), .a(maddr[9:0]),  .q(trom_q));

    always @(posedge clk) begin
        if (state == S_D) begin
            ram_q <= ram[maddr[9:0]];
            if (op == OP_STW) ram[maddr[9:0]] <= regs[rd];
        end
    end

    // ---- serial multiplier: 2 bits per clock, 16 clocks ----
    reg  [63:0] macc;
    reg  [63:0] mand;
    reg  [31:0] mier;
    reg         mneg;
    reg  [4:0]  mcnt;
    wire [31:0] absA = A[31] ? -A : A;
    wire [31:0] absB = B[31] ? -B : B;
    wire [63:0] prod = mneg ? (~macc + 64'd1) : macc;
    wire [63:0] prodq = prod + 64'd8192;

    // ---- execute ----
    reg  [31:0] res;
    reg         wr;
    reg  [10:0] npc;
    reg         taken;
    reg  [9:0]  smp_a;                  // first PWM value of a pair (OUT port 5)
    reg         pair;
    wire        stall = (op == OP_OUT) && (imm == 18'd1) && smp_full;

    // register write enable, written as plain logic (a case statement here is turned into a ROM
    // by the synthesizer, which then prevents mapping the program ROM to block RAM)
    wire wr_w = (op == OP_LI)   | (op == OP_LUI)  | (op == OP_ADD) | (op == OP_ADDI) | (op == OP_SUB) |
                (op == OP_AND)  | (op == OP_ANDI) | (op == OP_OR)  | (op == OP_ORI)  | (op == OP_XOR) |
                (op == OP_SLL)  | (op == OP_SRL)  | (op == OP_SRA) | (op == OP_MUL)  | (op == OP_MULQ) |
                (op == OP_SLT)  | (op == OP_SLTI) | (op == OP_LDB) | (op == OP_LDT)  | (op == OP_LDW) |
                (op == OP_IN);

    always @* begin
        res = 32'd0;
        wr = wr_w;
        npc = pc + 11'd1;
        case (op)
            OP_LI:   begin res = simm; end
            OP_LUI:  begin res = {imm, 14'd0}; end
            OP_ADD:  begin res = A + B; end
            OP_ADDI: begin res = A + simm; end
            OP_SUB:  begin res = A - B; end
            OP_AND:  begin res = A & B; end
            OP_ANDI: begin res = A & {14'd0, imm}; end
            OP_OR:   begin res = A | B; end
            OP_ORI:  begin res = A | {14'd0, imm}; end
            OP_XOR:  begin res = A ^ B; end
            OP_SLL:  begin res = A << imm[4:0]; end
            OP_SRL:  begin res = A >> imm[4:0]; end
            OP_SRA:  begin res = A >>> imm[4:0]; end
            OP_MUL:  begin res = prod[31:0]; end
            OP_MULQ: begin res = prodq[45:14]; end
            OP_SLT:  begin res = (A < B) ? 32'd1 : 32'd0; end
            OP_SLTI: begin res = (A < simm) ? 32'd1 : 32'd0; end
            OP_LDB:  begin res = {24'd0, brom_q}; end
            OP_LDT:  begin res = {{16{trom_q[15]}}, trom_q}; end
            OP_LDW:  begin res = ram_q; end
            OP_IN:   begin
                case (imm[3:0])
                    4'd2:    res = {24'd0, lb_data};
                    4'd3:    res = {24'd0, lb_len};
                    default: res = 32'd0;
                endcase
            end
            default: ;
        endcase
        taken = 1'b0;
        case (op)
            OP_BEQ: taken = (A == Bd);
            OP_BNE: taken = (A != Bd);
            OP_BLT: taken = (A <  Bd);
            OP_BGE: taken = (A >= Bd);
            default: ;
        endcase
        if (taken)            npc = pc + 11'd1 + simm[10:0];
        if (op == OP_JMP)     npc = imm[10:0];
        if (op == OP_CALL)    npc = imm[10:0];
        if (op == OP_RET)     npc = regs[15][10:0];
    end

    always @(posedge clk) begin
        smp_we <= 1'b0;
        if (rst) begin
            state <= S_HALT;
            done  <= 1'b1;
            pc    <= 11'd0;
            pair  <= 1'b0;
            lb_addr <= 7'd0;
        end else if (start) begin
            state <= S_F;
            done  <= 1'b0;
            pc    <= 11'd0;
        end else begin
            case (state)
                S_HALT: ;
                S_F: state <= S_D;
                S_D: begin
                    if (op == OP_MUL || op == OP_MULQ) begin
                        macc <= 64'd0;
                        mand <= {32'd0, absA};
                        mier <= absB;
                        mneg <= A[31] ^ B[31];
                        mcnt <= 5'd16;
                        state <= S_M;
                    end else begin
                        state <= S_E;
                    end
                end
                S_M: begin
                    macc <= macc + (mier[0] ? mand : 64'd0) + (mier[1] ? {mand[62:0], 1'b0} : 64'd0);
                    mand <= {mand[61:0], 2'b00};
                    mier <= {2'b00, mier[31:2]};
                    mcnt <= mcnt - 5'd1;
                    if (mcnt == 5'd1) state <= S_E;
                end
                S_E: begin
                    if (op == OP_HALT) begin
                        state <= S_HALT;
                        done  <= 1'b1;
                    end else if (!stall) begin
                        if (wr)           regs[rd] <= res;
                        if (op == OP_CALL) regs[15] <= {21'd0, pc} + 32'd1;
                        if (op == OP_OUT) begin
                            if (imm == 18'd1) begin
                                smp_we   <= 1'b1;
                                smp_data <= pair ? {regs[rd][9:0], smp_a} : {regs[rd][9:0], regs[rd][9:0]};
                                pair     <= 1'b0;
                            end else if (imm == 18'd5) begin
                                smp_a <= regs[rd][9:0];
                                pair  <= 1'b1;
                            end else if (imm == 18'd2) begin
                                lb_addr <= regs[rd][6:0];
                            end
                        end
                        pc    <= npc;
                        state <= S_F;
                    end
                end
                default: state <= S_HALT;
            endcase
        end
    end
endmodule
