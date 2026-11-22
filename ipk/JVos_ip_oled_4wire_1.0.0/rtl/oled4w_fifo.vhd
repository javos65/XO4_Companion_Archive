-------------------------------------------------------------------------------
-- oled4w_fifo : small synchronous FIFO, one clock, show-ahead read data.
--   * fill counter based flags (full / empty), no falling edge logic
--   * every register has exactly one driving process
--   * write while full / read while empty is ignored
-------------------------------------------------------------------------------
library ieee;
use ieee.std_logic_1164.all;

entity oled4w_fifo is
  generic (
    g_WIDTH : integer := 9;
    g_DEPTH : integer := 16
  );
  port (
    clk_i     : in  std_logic;
    rst_n_i   : in  std_logic;
    wr_en_i   : in  std_logic;
    wr_data_i : in  std_logic_vector(g_WIDTH-1 downto 0);
    rd_en_i   : in  std_logic;
    rd_data_o : out std_logic_vector(g_WIDTH-1 downto 0);
    full_o    : out std_logic;
    empty_o   : out std_logic
  );
end oled4w_fifo;

architecture rtl of oled4w_fifo is
  type t_mem is array (0 to g_DEPTH-1) of std_logic_vector(g_WIDTH-1 downto 0);
  signal mem      : t_mem;
  signal wr_idx   : integer range 0 to g_DEPTH-1;
  signal rd_idx   : integer range 0 to g_DEPTH-1;
  signal count    : integer range 0 to g_DEPTH;
  signal full     : std_logic;
  signal empty    : std_logic;
  signal do_wr    : std_logic;
  signal do_rd    : std_logic;
begin
  full    <= '1' when count = g_DEPTH else '0';
  empty   <= '1' when count = 0       else '0';
  do_wr   <= wr_en_i and (not full);
  do_rd   <= rd_en_i and (not empty);
  full_o  <= full;
  empty_o <= empty;
  rd_data_o <= mem(rd_idx);

  -- storage, write only
  p_mem : process (clk_i)
  begin
    if rising_edge(clk_i) then
      if do_wr = '1' then
        mem(wr_idx) <= wr_data_i;
      end if;
    end if;
  end process p_mem;

  -- pointers and fill counter
  p_ctrl : process (clk_i, rst_n_i)
  begin
    if rst_n_i = '0' then
      wr_idx <= 0;
      rd_idx <= 0;
      count  <= 0;
    elsif rising_edge(clk_i) then
      if do_wr = '1' then
        if wr_idx = g_DEPTH-1 then wr_idx <= 0; else wr_idx <= wr_idx + 1; end if;
      end if;
      if do_rd = '1' then
        if rd_idx = g_DEPTH-1 then rd_idx <= 0; else rd_idx <= rd_idx + 1; end if;
      end if;
      if (do_wr = '1') and (do_rd = '0') then
        count <= count + 1;
      elsif (do_wr = '0') and (do_rd = '1') then
        count <= count - 1;
      end if;
    end if;
  end process p_ctrl;
end rtl;
