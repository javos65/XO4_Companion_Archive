-------------------------------------------------------------------------------
-- oled4w_spi : 4-wire SPI transmitter for SSD1306 style OLED (write only)
--   SPI mode 0 : SCLK idles low, data changes while SCLK is low, the display
--   samples on the rising edge. MSB first, 8 bits per byte. D/C# is a separate
--   pin taken from bit 8 of the word. CS# is low only while a byte is shifted.
--   One bit period = 2 ticks, one tick = (clkdiv_i + 1) clk_i cycles, so
--   SCLK = clk_i / (2 * (clkdiv_i + 1)).
--   Timeline of one byte (ticks):  CS low | data | clk high | data | clk high ...
--                                  ... clk high | clk low (CS hold) | CS high
-------------------------------------------------------------------------------
library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;

entity oled4w_spi is
  port (
    clk_i    : in  std_logic;
    rst_n_i  : in  std_logic;
    clkdiv_i : in  std_logic_vector(7 downto 0);
    tx_valid_i : in  std_logic;                      -- a word is waiting (FIFO not empty)
    tx_data_i  : in  std_logic_vector(8 downto 0);   -- [8] D/C#, [7:0] byte
    tx_pop_o   : out std_logic;                      -- one cycle pulse: word taken
    busy_o     : out std_logic;
    sclk_o   : out std_logic;
    sdin_o   : out std_logic;
    dc_o     : out std_logic;
    cs_o     : out std_logic
  );
end oled4w_spi;

architecture rtl of oled4w_spi is
  type t_state is (S_IDLE, S_DATA, S_CLK, S_END, S_DONE);
  signal state : t_state;
  signal tcnt  : unsigned(7 downto 0);
  signal tick  : std_logic;
  signal sh    : std_logic_vector(7 downto 0);
  signal bcnt  : integer range 0 to 7;
begin
  tick <= '1' when tcnt = 0 else '0';
  busy_o <= '0' when state = S_IDLE else '1';

  p_spi : process (clk_i, rst_n_i)
  begin
    if rst_n_i = '0' then
      state    <= S_IDLE;
      tcnt     <= (others => '0');
      sh       <= (others => '0');
      bcnt     <= 7;
      tx_pop_o <= '0';
      sclk_o   <= '0';
      sdin_o   <= '0';
      dc_o     <= '0';
      cs_o     <= '1';
    elsif rising_edge(clk_i) then
      tx_pop_o <= '0';

      -- tick generator
      if tcnt = 0 then
        tcnt <= unsigned(clkdiv_i);
      else
        tcnt <= tcnt - 1;
      end if;

      case state is
        when S_IDLE =>
          sclk_o <= '0';
          cs_o   <= '1';
          if tx_valid_i = '1' then
            sh       <= tx_data_i(7 downto 0);
            dc_o     <= tx_data_i(8);       -- D/C# stable before CS falls
            tx_pop_o <= '1';
            bcnt     <= 7;
            tcnt     <= unsigned(clkdiv_i); -- first tick is a full tick away
            cs_o     <= '0';
            state    <= S_DATA;
          end if;

        when S_DATA =>                       -- clock low, present next bit
          if tick = '1' then
            sclk_o <= '0';
            sdin_o <= sh(7);
            sh     <= sh(6 downto 0) & '0';
            state  <= S_CLK;
          end if;

        when S_CLK =>                        -- clock high, display samples
          if tick = '1' then
            sclk_o <= '1';
            if bcnt = 0 then
              state <= S_END;
            else
              bcnt  <= bcnt - 1;
              state <= S_DATA;
            end if;
          end if;

        when S_END =>                        -- clock low again, CS still low
          if tick = '1' then
            sclk_o <= '0';
            state  <= S_DONE;
          end if;

        when S_DONE =>                       -- release CS
          if tick = '1' then
            cs_o   <= '1';
            sdin_o <= '0';
            state  <= S_IDLE;
          end if;
      end case;
    end if;
  end process p_spi;
end rtl;
