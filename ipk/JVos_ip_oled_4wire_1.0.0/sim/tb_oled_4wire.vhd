library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;

entity tb_oled_4wire is end tb_oled_4wire;

architecture sim of tb_oled_4wire is
  signal clk, rstn : std_logic := '0';
  signal psel, penable, pwrite : std_logic := '0';
  signal paddr  : std_logic_vector(7 downto 0)  := (others => '0');
  signal pwdata : std_logic_vector(31 downto 0) := (others => '0');
  signal prdata : std_logic_vector(31 downto 0);
  signal pready, pslverr, sclk, sdin, dc, cs, res_n, vbatc_n, vddc_n : std_logic;
  signal done : boolean := false;
  type byte_arr is array (0 to 255) of std_logic_vector(8 downto 0);
  signal rx : byte_arr;
  signal rx_n : integer := 0;
  signal sclk_hz_chk : integer := 0;
begin
  clk <= not clk after 20.833 ns when not done;      -- 24 MHz

  dut : entity work.oled_4wire
    port map (clk_i => clk, rst_n_i => rstn, PSEL => psel, PENABLE => penable, PADDR => paddr,
              PWRITE => pwrite, PWDATA => pwdata, PRDATA => prdata, PREADY => pready,
              PSLVERR => pslverr, SCLK => sclk, SDIN => sdin, DC => dc, CS => cs,
              RES_N => res_n, VBATC_N => vbatc_n, VDDC_N => vddc_n);

  -- SPI slave model: mode 0, MSB first, D/C sampled when CS falls
  monitor : process
    variable sh : std_logic_vector(8 downto 0);
    variable n  : integer;
  begin
    wait until falling_edge(cs);
    n := 0; sh := (others => '0'); sh(8) := dc;
    while cs = '0' loop
      wait until rising_edge(sclk) or rising_edge(cs);
      if cs = '0' then
        sh(7 downto 0) := sh(6 downto 0) & sdin; n := n + 1;
      end if;
    end loop;
    assert n = 8 report "bad bit count " & integer'image(n) severity error;
    rx(rx_n) <= sh; rx_n <= rx_n + 1;
  end process;

  stim : process
    procedure apb_write(a : std_logic_vector(7 downto 0); d : std_logic_vector(31 downto 0)) is
    begin
      wait until rising_edge(clk);
      psel <= '1'; penable <= '0'; pwrite <= '1'; paddr <= a; pwdata <= d;
      wait until rising_edge(clk);
      penable <= '1';
      wait until rising_edge(clk);
      assert pready = '1' report "PREADY low" severity error;
      psel <= '0'; penable <= '0'; pwrite <= '0';
    end procedure;
    procedure apb_read(a : std_logic_vector(7 downto 0); d : out std_logic_vector(31 downto 0)) is
    begin
      wait until rising_edge(clk);
      psel <= '1'; penable <= '0'; pwrite <= '0'; paddr <= a;
      wait until rising_edge(clk);
      penable <= '1';
      wait until falling_edge(clk);
      d := prdata;
      wait until rising_edge(clk);
      psel <= '0'; penable <= '0';
    end procedure;
    variable s : std_logic_vector(31 downto 0);
    variable t0, t1 : time;
  begin
    wait for 200 ns; rstn <= '1'; wait for 200 ns;
    apb_read(x"00", s); assert s = x"4F4C4401" report "ID wrong" severity error;
    apb_read(x"08", s);
    assert s(10 downto 0) = "11000000001" report "CONTROL reset value wrong" severity error;
    assert res_n = '0' and vbatc_n = '1' and vddc_n = '1' and cs = '1' and sclk = '0'
      report "pins after reset wrong" severity error;
    apb_read(x"04", s); assert s(3 downto 0) = "1010" report "STATUS after reset wrong" severity error;

    -- power sequence bits: VDDC low, RES high, then VBATC low
    -- CONTROL[10:8] = VDDC_N, VBATC_N, RES_N
    apb_write(x"08", x"00000" & "0" & "010" & x"01"); wait for 100 ns;   -- VDDC on
    assert vddc_n = '0' and vbatc_n = '1' and res_n = '0' report "VDDC not switched" severity error;
    apb_write(x"08", x"00000" & "0" & "011" & x"01"); wait for 100 ns;   -- release RES
    assert res_n = '1' and vddc_n = '0' and vbatc_n = '1' report "RES release wrong" severity error;
    apb_write(x"08", x"00000" & "0" & "001" & x"01"); wait for 100 ns;   -- VBAT on
    assert vbatc_n = '0' and vddc_n = '0' and res_n = '1' report "VBATC not switched" severity error;

    -- 40 words, polling FULL (STATUS[2]) like the driver does
    for i in 0 to 39 loop
      loop
        apb_read(x"04", s);
        exit when s(2) = '0';
      end loop;
      apb_write(x"0C", x"00000" & "000" & std_logic_vector(to_unsigned(i mod 2, 1)) & std_logic_vector(to_unsigned(16#40# + i, 8)));
    end loop;
    -- wait until everything is sent
    loop
      apb_read(x"04", s);
      exit when s(3) = '1';
    end loop;
    assert rx_n = 40 report "received " & integer'image(rx_n) & " words, expected 40" severity error;
    for i in 0 to 39 loop
      assert rx(i)(7 downto 0) = std_logic_vector(to_unsigned(16#40# + i, 8)) and ((rx(i)(8) = '1') = (i mod 2 = 1))
        report "mismatch word " & integer'image(i) severity error;
    end loop;

    -- overflow: 20 quick writes without polling (FIFO depth is 16)
    for i in 0 to 19 loop
      apb_write(x"0C", x"000001" & std_logic_vector(to_unsigned(i, 8)));
    end loop;
    apb_read(x"04", s); assert s(4) = '1' report "overflow flag not set" severity error;
    apb_write(x"04", x"00000010");
    apb_read(x"04", s); assert s(4) = '0' report "overflow flag not cleared" severity error;
    loop apb_read(x"04", s); exit when s(3) = '1'; end loop;
    -- 16 words fit the FIFO, the shifter drains 1-2 more during the burst
    assert rx_n >= 56 and rx_n <= 60 report "unexpected word count after overflow test: " & integer'image(rx_n) severity error;

    -- SCLK frequency: CLKDIV=1 -> 24MHz / 4 = 6 MHz (period ~166.7 ns)
    apb_write(x"0C", x"00000155");
    wait until rising_edge(sclk); t0 := now; wait until rising_edge(sclk); t1 := now;
    assert (t1 - t0) > 160 ns and (t1 - t0) < 175 ns report "SCLK period wrong: " & time'image(t1 - t0) severity error;
    loop apb_read(x"04", s); exit when s(3) = '1'; end loop;

    report "TEST DONE, words received: " & integer'image(rx_n);
    done <= true;
    wait;
  end process;
end sim;
