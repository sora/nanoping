module top (
	input clock,
	input reset_n,
	// Ethernet PHY#1
	output phy1_rst_n,
	input phy1_125M_clk,
	input phy1_tx_clk,
	output phy1_gtx_clk,
	output phy1_tx_en,
	output [7:0] phy1_tx_data,
	input phy1_rx_clk,
	input phy1_rx_dv,
	input phy1_rx_er,
	input [7:0] phy1_rx_data,
	input phy1_col,
	input phy1_crs,
	output phy1_mii_clk,
	inout phy1_mii_data,
	// Switch/LED
	input [7:0] switch,
	output [7:0] led
);
reg [8:0] coldsys_rst = 0;
wire coldsys_rst260 = (coldsys_rst==9'd260);
always @(posedge clock)
	coldsys_rst <= !coldsys_rst260 ? coldsys_rst + 9'd1 : 9'd260;
reg [11:0] counter;
reg [7:0] tx_data;
reg tx_en;
wire crc_init = (counter == 12'h08);
wire [31:0] crc_out;
reg crc_rd;
wire crc_data_en = ~crc_rd;
crc_gen crc_inst ( .Reset(~reset_n), .Clk(phy1_125M_clk), .Init(crc_init), .Frame_data(tx_data), .Data_en(crc_data_en), .CRC_rd(crc_rd), .CRC_end(), .CRC_out(crc_out)); 
always @(posedge phy1_125M_clk) begin
	if (reset_n == 1'b0) begin
		crc_rd <= 1'b0;
		tx_data <= 8'h00;
		tx_en <= 1'b0;
		counter <= 12'd0;
	end else begin
		case (counter)
			12'h00: begin
				tx_data <= 8'h55;
				tx_en <= 1'b1;
			end
			12'h01: tx_data <= 8'h55;	// Preamble
			12'h02: tx_data <= 8'h55;
			12'h03: tx_data <= 8'h55;
			12'h04: tx_data <= 8'h55;
			12'h05: tx_data <= 8'h55;
			12'h06: tx_data <= 8'h55;
			12'h07: tx_data <= 8'hd5;	// Preable + Start Frame Delimiter
			
			12'h08: tx_data <= 8'ha0;	// Destination MAC address = FF-FF-FF-FF-FF-FF-FF
			12'h09: tx_data <= 8'h36;
			12'h0a: tx_data <= 8'h9f;
			12'h0b: tx_data <= 8'haf;
			12'h0c: tx_data <= 8'h12;
			12'h0d: tx_data <= 8'h9c;
			
			12'h0e: tx_data <= 8'h00;	// Source MAC address = 00-30-1b-a0-a4-8e
			12'h0f: tx_data <= 8'h30;
			12'h10: tx_data <= 8'h1b;
			12'h11: tx_data <= 8'ha0;
			12'h12: tx_data <= 8'ha4;
			12'h13: tx_data <= 8'h8e;
			
			12'h14: tx_data <= 8'h08;	// Protocol Type = IPv4 (0x0800)
			12'h15: tx_data <= 8'h00;
			12'h16: tx_data <= 8'h45;	// 
			12'h17: tx_data <= 8'h00;
			
			12'h18: tx_data <= 8'h00;	// ipv4 length (old: 00 45)
			12'h19: tx_data <= 8'h34;
			
			12'h1a: tx_data <= 8'h72;	//
			12'h1b: tx_data <= 8'h2d;	//
			12'h1c: tx_data <= 8'h40;	//
			12'h1d: tx_data <= 8'h00;
			12'h1e: tx_data <= 8'h40;	// 
			12'h1f: tx_data <= 8'h11;
			
			12'h20: tx_data <= 8'hb4;   // ipv4 checksum
			12'h21: tx_data <= 8'h87;
			
			12'h22: tx_data <= 8'h0a;   // ipv4_srcip
			12'h23: tx_data <= 8'h00;
			12'h24: tx_data <= 8'h00;
			12'h25: tx_data <= 8'h03;
			
			12'h26: tx_data <= 8'h0a;   // ipv4_dstip
			12'h27: tx_data <= 8'h00;
			12'h28: tx_data <= 8'h00;
			12'h29: tx_data <= 8'h02;
			
			12'h2a: tx_data <= 8'h14;   // src port
			12'h2b: tx_data <= 8'hca;
			
			12'h2c: tx_data <= 8'h14;   // dst port
			12'h2d: tx_data <= 8'hca;
			
			12'h2e: tx_data <= 8'h00;	// udp length (old: 00 31)
			12'h2f: tx_data <= 8'h20;
			
			12'h30: tx_data <= 8'h00;   // udp checksum
			12'h31: tx_data <= 8'h00;
			
			12'h32: tx_data <= 8'h22;	// nanoping magic (0x22334455
			12'h33: tx_data <= 8'h33;
			12'h34: tx_data <= 8'h44;
			12'h35: tx_data <= 8'h55;
			
			12'h36: tx_data <= 8'hAA;   // nanoping id 
			12'h37: tx_data <= 8'hAA;
			12'h38: tx_data <= 8'hAA;
			12'h39: tx_data <= 8'hAA;
			
			12'h3a: tx_data <= 8'h01;  // nanoping tx timestamp
			12'h3b: tx_data <= 8'h12;
			12'h3c: tx_data <= 8'h23;
			12'h3d: tx_data <= 8'h34;
			12'h3e: tx_data <= 8'h45;
			12'h3f: tx_data <= 8'h56;
			12'h40: tx_data <= 8'h67;
			12'h41: tx_data <= 8'h78;
			
			12'h42: tx_data <= 8'hff;   // nanoping rx timestamp
			12'h43: tx_data <= 8'hee;
			12'h44: tx_data <= 8'hdd;
			12'h45: tx_data <= 8'hcc;
			12'h46: tx_data <= 8'hbb;
			12'h47: tx_data <= 8'haa;
			12'h48: tx_data <= 8'h99;
			12'h49: tx_data <= 8'h88;
			
			12'h4a: begin				// Frame Check Sequence
				crc_rd  <= 1'b1;
				tx_data <= crc_out[31:24];
			end
			12'h4b: tx_data <= crc_out[23:16];
			12'h4c: tx_data <= crc_out[15:8];
			12'h4d: tx_data <= crc_out[7:0];
			12'h4e: begin
				tx_en <= 1'b0;
				crc_rd  <= 1'b0;
				tx_data <= 8'h00;
			end
			default: tx_data <= 8'h0;
		endcase
		counter <= counter + 12'd1;
	end
end
assign phy1_mii_clk = 1'b0;
assign phy1_mii_data = 1'b0;
assign phy1_tx_en = tx_en;
assign phy1_tx_data = tx_data;
assign phy1_gtx_clk = phy1_125M_clk;
assign phy1_rst_n = coldsys_rst260;
assign led[7:0] = tx_en ? 8'h0 : 8'hff;
endmodule
