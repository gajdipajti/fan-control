R25 = 4700;
K=3850;
TC=0:0.1:120;
T=TC+273;
T25=25+273;
RT=R25*e.^(K*(1./T-1./T25));

figure;
plot(TC,RT);
hold on;
line([0 25], [4700 4700], "color", "r");
line([25 25], [0 4700], "color", "r");
xlabel("Temperature °C");
ylabel("Resistance [Ohm]");
title("NTC resistance");
hold off;
