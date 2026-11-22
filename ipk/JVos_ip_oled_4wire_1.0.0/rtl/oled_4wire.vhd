-------------------------------------------------------------------------------
-- oled_4wire : APB slave, 4-wire SPI master for SSD1306 OLED (Digilent Pmod OLED)
--
--   Single clock domain (clk_i = PCLK), zero wait-state APB slave.
--   Register map (word offsets, PADDR[3:2] decoded):
--
--   0x00  ID       RO  0x4F4C4401
--   0x04  STATUS   RO  [0] BUSY      shifter is sending a byte
--                      [1] EMPTY     FIFO empty
--                      [2] FULL      FIFO full
--                      [3] IDLE      FIFO empty and shifter idle (all sent)
--                      [4] OVERFLOW  data written while FIFO full (sticky,
--                                    write 1 to bit 4 of STATUS to clear)
--   0x08  CONTROL  RW  [7:0] CLKDIV  SCLK = PCLK / (2 * (CLKDIV + 1)), reset 1
--                      [8]   RES_N   OLED reset pin   (reset value 0 = in reset)
--                      [9]   VBATC_N display supply   (reset value 1 = OFF, active low)
--                      [10]  VDDC_N  logic supply     (reset value 1 = OFF, active low)
--   0x0C  DATA     WO  [8] D/C# (1 = data, 0 = command), [7:0] byte -> FIFO (16 deep)
--                      (a read returns the last written value)
--
--   Pmod OLED: VBATC / VDDC drive P-channel power switches, active LOW.
-------------------------------------------------------------------------------
library ieee;
use ieee.std_logic_1164.all;

entity oled_4wire is
  port (
    clk_i   : in  std_logic;
    rst_n_i : in  std_logic;
    PSEL    : in  std_logic;
    PENABLE : in  std_logic;
    PADDR   : in  std_logic_vector(7 downto 0);
    PWRITE  : in  std_logic;
    PWDATA  : in  std_logic_vector(31 downto 0);
    PRDATA  : out std_logic_vector(31 downto 0);
    PREADY  : out std_logic;
    PSLVERR : out std_logic;
    SCLK    : out std_logic;
    SDIN    : out std_logic;
    DC      : out std_logic;
    CS      : out std_logic;
    RES_N   : out std_logic;
    VBATC_N : out std_logic;
    VDDC_N  : out std_logic
  );
end oled_4wire;

architecture rtl of oled_4wire is
  constant C_ID : std_logic_vector(31 downto 0) := x"4F4C4401";

  signal reg_sel    : std_logic_vector(1 downto 0);
  signal wr_en      : std_logic;                     -- APB access phase write
  signal status     : std_logic_vector(31 downto 0);
  signal control    : std_logic_vector(10 downto 0);
  signal last_data  : std_logic_vector(31 downto 0);
  signal overflow   : std_logic;

  signal fifo_wr    : std_logic;
  signal fifo_empty : std_logic;
  signal fifo_full  : std_logic;
  signal fifo_data  : std_logic_vector(8 downto 0);
  signal tx_valid   : std_logic;
  signal spi_pop    : std_logic;
  signal spi_busy   : std_logic;

  component oled4w_fifo
    generic (g_WIDTH : integer := 9; g_DEPTH : integer := 16);
    port (
      clk_i     : in  std_logic;
      rst_n_i   : in  std_logic;
      wr_en_i   : in  std_logic;
      wr_data_i : in  std_logic_vector(8 downto 0);
      rd_en_i   : in  std_logic;
      rd_data_o : out std_logic_vector(8 downto 0);
      full_o    : out std_logic;
      empty_o   : out std_logic
    );
  end component;

  component oled4w_spi
    port (
      clk_i      : in  std_logic;
      rst_n_i    : in  std_logic;
      clkdiv_i   : in  std_logic_vector(7 downto 0);
      tx_valid_i : in  std_logic;
      tx_data_i  : in  std_logic_vector(8 downto 0);
      tx_pop_o   : out std_logic;
      busy_o     : out std_logic;
      sclk_o     : out std_logic;
      sdin_o     : out std_logic;
      dc_o       : out std_logic;
      cs_o       : out std_logic
    );
  end component;
begin
  reg_sel <= PADDR(3 downto 2);
  wr_en   <= PSEL and PENABLE and PWRITE;

  PREADY  <= '1';
  PSLVERR <= '0';

  tx_valid <= not fifo_empty;

  -- accept a data word only when the FIFO has room
  fifo_wr <= wr_en and (not fifo_full) when reg_sel = "11" else '0';

  u_fifo : oled4w_fifo
    generic map (g_WIDTH => 9, g_DEPTH => 16)
    port map (
      clk_i     => clk_i,
      rst_n_i   => rst_n_i,
      wr_en_i   => fifo_wr,
      wr_data_i => PWDATA(8 downto 0),
      rd_en_i   => spi_pop,
      rd_data_o => fifo_data,
      full_o    => fifo_full,
      empty_o   => fifo_empty
    );

  u_spi : oled4w_spi
    port map (
      clk_i      => clk_i,
      rst_n_i    => rst_n_i,
      clkdiv_i   => control(7 downto 0),
      tx_valid_i => tx_valid,
      tx_data_i  => fifo_data,
      tx_pop_o   => spi_pop,
      busy_o     => spi_busy,
      sclk_o     => SCLK,
      sdin_o     => SDIN,
      dc_o       => DC,
      cs_o       => CS
    );

  RES_N   <= control(8);
  VBATC_N <= control(9);
  VDDC_N  <= control(10);

  status(31 downto 5) <= (others => '0');
  status(4) <= overflow;
  status(3) <= fifo_empty and (not spi_busy);
  status(2) <= fifo_full;
  status(1) <= fifo_empty;
  status(0) <= spi_busy;

  -- read mux, valid during the whole access
  PRDATA <= C_ID       when reg_sel = "00" else
            status     when reg_sel = "01" else
            (31 downto 11 => '0') & control when reg_sel = "10" else
            last_data;

  p_regs : process (clk_i, rst_n_i)
  begin
    if rst_n_i = '0' then
      control   <= "110" & x"01";     -- VDDC off, VBATC off, RES asserted, CLKDIV = 1
      last_data <= (others => '0');
      overflow  <= '0';
    elsif rising_edge(clk_i) then
      if wr_en = '1' then
        case reg_sel is
          when "01" =>
            if PWDATA(4) = '1' then overflow <= '0'; end if;
          when "10" =>
            control <= PWDATA(10 downto 0);
          when "11" =>
            last_data <= PWDATA;
            if fifo_full = '1' then overflow <= '1'; end if;
          when others =>
            null;
        end case;
      end if;
    end if;
  end process p_regs;
end rtl;
