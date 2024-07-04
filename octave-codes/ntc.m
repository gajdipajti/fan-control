R25 = 4700;
K=3950;
TC=0:0.1:120;
T=TC+273;
T25=25+273;
RT=R25*e.^(K*(1./T-1./T25));

figure;
subplot(1,2,1);
plot(TC,RT);
hold on;
line([0 25], [4700 4700], "color", "r");
line([25 25], [0 4700], "color", "r");
xlabel("Temperature °C");
ylabel("Resistance [Ohm]");
title("NTC resistance");
hold off;

subplot(1,2,2);
semilogx(TC,RT);
hold on;
line([0 25], [4700 4700], "color", "r");
line([25 25], [0 4700], "color", "r");
xlabel("Temperature °C");
ylabel("Resistance [Ohm]");
title("NTC resistance");
hold off;
