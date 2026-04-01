% Matlab document used to plot the recollected data.
% Plots the collected experimental data (from rx_distance_data.csv) and compares it with the ideal Friis Law curve.


file = 'rx_distance_data.csv';
data = readtable(file);

distances = data{:, 1}; %1st colum is distance in meters.
rx_powers = data{:, 2}; % 2nd is the RX power in dBm.

valid_indexes = distances > 0;  % Only takes distances >0 
distances = distances(valid_indexes);
rx_powers = rx_powers(valid_indexes);

figure('Name', 'RX Power vs Distance','Color','w');
scatter(distances,rx_powers,15,'b','filled','MarkerFaceAlpha',0.5,'DisplayName',' Experimental data');
title('RX Power - Distance correlation');
xlabel('Distance (m)');
ylabel('RX Power (dBm)');

grid minor;

hold on;

ideal_distances = linspace(1,110,500);
ideal_rx_powers = -63.0 - 20*log10(ideal_distances);
plot(ideal_distances,ideal_rx_powers,'r','LineWidth',1.5,'DisplayName',' Ideal (Friis Law)');
legend;
hold off;