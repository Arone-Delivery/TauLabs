
%% full model on desired output with parameter learning

% this is a dynamic model that takes control inputs, applies a LPF
% and then an adjustable gain to generate the rotation rate. it
% allows learning the time constant of the LPF as well as the
% gain for each axis. it must be sufficiently excited by inputs
% to converge.

% state variables:
%   - w1 - roll rotation
%   - w2 - pitch rotation
%   - u1 - roll force
%   - u2 - pitch force
%   - b1 - roll gain
%   - b2 - pitch gain
%   - tau

% the actuator inputs (four motor speeds)

syms b1 b2 w1 w2 u1 u2 bias1 bias2 tau Ts real;
syms u1_in u2_in real;

x = [w1 w2 u1 u2 b1 b2 tau bias1 bias2]';
u_in = [u1_in u2_in ]';

% this is the mixer matrix (could be learned in more detail)
A_w = [1 0 Ts*exp(b1)  0          ; ...
       0 1 0           Ts*exp(b2) ];

A_u = [exp(tau)/(exp(tau) + Ts) 0; ...
       0 exp(tau)/(exp(tau) + Ts)];

A_wb = [-Ts*exp(b1) 0;
        0 -Ts*exp(b2)];

A_p = eye(3);

%w1 w2 w3 u1 u2 u3 b1 b2 b3 tau tau_y bias1 bias2 bias3
A = [     A_w                   zeros(2,3)     A_wb; ...
     zeros(2,2)       A_u       zeros(2,3)  zeros(2,2); ...
     zeros(3,2)     zeros(3,2)  A_p         zeros(3,2); ...
     zeros(2,7)                              eye(2)];
 
B_u = [zeros(2); ...
       Ts/(exp(tau) + Ts) 0; ...
       0 Ts/(exp(tau) + Ts); ...
       zeros(5,2)];

f = A * x + B_u * u_in

h = [w1 w2]'

F = simplify(jacobian(f, x), 100)

H = jacobian(h, x)

N = length(x)

%% generate the symbolic code

% this substitution cuts down on a ton of multiplications since
% we are using discrete time and these are always multiplied
syms Aeb1 Aeb2 Aeb3 Tstau real
A = subs(A, 'Ts*exp(b1)', Aeb1);
A = subs(A, 'Ts*exp(b2)', Aeb2);
A = subs(A, 'Ts*exp(b3)', Aeb3);
A = subs(A, 'Ts*exp(b3)', Aeb3);

F = subs(F, 'Ts*exp(b1)', Aeb1);
F = subs(F, 'Ts*exp(b2)', Aeb2);
F = subs(F, 'Ts*exp(b3)', Aeb3);

syms P_1_1 P_1_2 P_1_3 P_1_4 P_1_5 P_1_6 P_1_7 P_1_8 P_1_9  real
syms P_2_2 P_2_3 P_2_4 P_2_5 P_2_6 P_2_7 P_2_8 P_2_9  real
syms P_3_3 P_3_4 P_3_5 P_3_6 P_3_7 P_3_8 P_3_9  real
syms P_4_4 P_4_5 P_4_6 P_4_7 P_4_8 P_4_9  real
syms P_5_5 P_5_6 P_5_7 P_5_8 P_5_9  real
syms P_6_6 P_6_7 P_6_8 P_6_9  real
syms P_7_7 P_7_8 P_7_9  real
syms P_8_8 P_8_9  real
syms P_9_9  real

syms s_a real

syms gyro_x gyro_y real

syms Q_1 Q_2 Q_3 Q_4 Q_5 Q_6 Q_7 Q_8 Q_9  real

y = [gyro_x gyro_y]' - h;

P=[
P_1_1 P_1_2 P_1_3 P_1_4 P_1_5 P_1_6 P_1_7 P_1_8 P_1_9 ;
0     P_2_2 P_2_3 P_2_4 P_2_5 P_2_6 P_2_7 P_2_8 P_2_9 ;
0     0     P_3_3 P_3_4 P_3_5 P_3_6 P_3_7 P_3_8 P_3_9 ;
0     0     0     P_4_4 P_4_5 P_4_6 P_4_7 P_4_8 P_4_9 ;
0     0     0     0     P_5_5 P_5_6 P_5_7 P_5_8 P_5_9 ;
0     0     0     0     0     P_6_6 P_6_7 P_6_8 P_6_9 ;
0     0     0     0     0     0     P_7_7 P_7_8 P_7_9 ;
0     0     0     0     0     0     0     P_8_8 P_8_9 ;
0     0     0     0     0     0     0     0     P_9_9 ;
];

%x = [w1 w2 u1 u2 b1 b2 tau bias1 bias2]';

% remove cross coupling terms in the covariance
P([1 3 5 8],[2 4 6 9]) = 0;
P([2 4 6 9],[1 3 5 8]) = 0;

% we can use this variable to reduce the unused terms out of the equations
% below instead of storing all of the P values.
P_idx = find(P(:));

% make it symmetrical
for(i=2:N)
    for (j=1:i-1)
        P(i,j)=P(j,i);
    end
end
       
Q = diag([Q_1 Q_2 Q_3 Q_4 Q_5 Q_6 Q_7 Q_8 Q_9]);

P2 = (F*P*F') + Q;
aeb1_opt = [1 7 13 20];
aeb2_opt = [2 8 14 21];
for i = aeb1_opt
    P2(P_idx(aeb1_opt)) = collect(P2(P_idx(aeb1_opt)),Aeb1);
end
for i = aeb2_opt
    P2(P_idx(aeb2_opt)) = collect(P2(P_idx(aeb2_opt)),Aeb2);
end

P2 = simplify(P2,5);


% update equations
R = diag([s_a s_a]); 
S = H*P*H' + R;

% remove coupling between axes for efficiency. from the above equation
% we can see that S_1 should be P[0][0] + s_a, S_2 is P[1][1] + s_a
% etc
syms S_1 S_2 real
S = diag([S_1 S_2])
  
K = P*H'/S;

x_new = x + K*y;

I = eye(length(K));
P3 = (I - K*H)*P;  % Output state covariance

%% create strings for update equations


fid = fopen('autotune_filter_2d.c','w');


for Pnew = {P2, P3}
    nummults=0;
    numadds =0;

    fprintf(fid, '\n\n\n\n\t// Covariance calculation\n');
    Pnew = simplify(Pnew{1});


    Pstrings=cell(N,N);
    for i=1:N
        for j=i:N
            if P(i,j) == 0
                Pstrings{i,j} = num2str(0);
            else           
                % replace with lots of precalculated constants or invalid C
                Pstrings{i,j} = char(Pnew(i,j));
                Pstrings{i,j} = strrep(Pstrings{i,j},'Aeb1^2','Aeb1_2');
                Pstrings{i,j} = strrep(Pstrings{i,j},'Aeb2^2','Aeb2_2');
                Pstrings{i,j} = strrep(Pstrings{i,j},'Aeb3^2','Aeb3_2');
                Pstrings{i,j} = strrep(Pstrings{i,j},'Ts^2','Tsq');
                Pstrings{i,j} = strrep(Pstrings{i,j},'Ts^3','Tsq3');
                Pstrings{i,j} = strrep(Pstrings{i,j},'Ts^4','Tsq4');
                Pstrings{i,j} = strrep(Pstrings{i,j},'P','D');
                Pstrings{i,j} = strrep(Pstrings{i,j},'exp(b1)','e_b1');
                Pstrings{i,j} = strrep(Pstrings{i,j},'exp(b2)','e_b2');
                Pstrings{i,j} = strrep(Pstrings{i,j},'exp(b3)','e_b3');
                Pstrings{i,j} = strrep(Pstrings{i,j},'exp(b3d)','e_b3d');
                Pstrings{i,j} = strrep(Pstrings{i,j},'exp(2*b1)','(e_b1*e_b1)');
                Pstrings{i,j} = strrep(Pstrings{i,j},'exp(2*b2)','(e_b2*e_b2)');
                Pstrings{i,j} = strrep(Pstrings{i,j},'exp(2*b3)','(e_b3*e_b3)');
                Pstrings{i,j} = strrep(Pstrings{i,j},'exp(2*b3d)','(e_b3d*e_b3d)');
                Pstrings{i,j} = strrep(Pstrings{i,j},'exp(2*tau)','e_tau2');
                Pstrings{i,j} = strrep(Pstrings{i,j},'exp(3*tau)','e_tau3');
                Pstrings{i,j} = strrep(Pstrings{i,j},'exp(4*tau)','e_tau4');
                Pstrings{i,j} = strrep(Pstrings{i,j},'exp(tau)','e_tau');
                Pstrings{i,j} = strrep(Pstrings{i,j},'exp(2*tau_y)','e_tau_y2');
                Pstrings{i,j} = strrep(Pstrings{i,j},'exp(3*tau_y)','e_tau_y3');
                Pstrings{i,j} = strrep(Pstrings{i,j},'exp(4*tau_y)','e_tau_y4');
                Pstrings{i,j} = strrep(Pstrings{i,j},'exp(tau_y)','e_tau_y');
                Pstrings{i,j} = strrep(Pstrings{i,j},'s_a^2','s_a2');
                Pstrings{i,j} = strrep(Pstrings{i,j},'s_a^3','s_a3');
                Pstrings{i,j} = strrep(Pstrings{i,j},'u1^2','(u1*u1)');
                Pstrings{i,j} = strrep(Pstrings{i,j},'u2^2','(u2*u2)');
                Pstrings{i,j} = strrep(Pstrings{i,j},'u3^2','(u3*u3)');
                Pstrings{i,j} = strrep(Pstrings{i,j},'bias1^2','(bias1*bias1)');
                Pstrings{i,j} = strrep(Pstrings{i,j},'bias2^2','(bias2*bias2)');
                Pstrings{i,j} = strrep(Pstrings{i,j},'bias3^2','(bias3*bias3)');
                Pstrings{i,j} = strrep(Pstrings{i,j},'u1_in^2','(u1_in*u1_in)');
                Pstrings{i,j} = strrep(Pstrings{i,j},'u2_in^2','(u2_in*u2_in)');
                Pstrings{i,j} = strrep(Pstrings{i,j},'u3_in^2','(u3_in*u3_in)');
                Pstrings{i,j} = strrep(Pstrings{i,j},'(Ts + e_tau)^2','Ts_e_tau2');
                Pstrings{i,j} = strrep(Pstrings{i,j},'(Ts + e_tau)^3','Ts_e_tau3');
                Pstrings{i,j} = strrep(Pstrings{i,j},'(Ts + e_tau)^4','Ts_e_tau4');
                Pstrings{i,j} = strrep(Pstrings{i,j},'(Ts + e_tau_y)^2','Ts_e_tau_y2');
                Pstrings{i,j} = strrep(Pstrings{i,j},'(Ts + e_tau_y)^3','Ts_e_tau_y3');
                Pstrings{i,j} = strrep(Pstrings{i,j},'(Ts + e_tau_y)^4','Ts_e_tau_y4');
                Pstrings{i,j} = strrep(Pstrings{i,j},'exp(b1 + tau)','(e_b1*e_tau)');
                Pstrings{i,j} = strrep(Pstrings{i,j},'exp(b2 + tau)','(e_b2*e_tau)');
                Pstrings{i,j} = strrep(Pstrings{i,j},'exp(b3 + tau_y)','(e_b3*e_tau_y)');
                Pstrings{i,j} = strrep(Pstrings{i,j},'exp(b1 + 2*tau)','(e_b1*e_tau2)');
                Pstrings{i,j} = strrep(Pstrings{i,j},'exp(b2 + 2*tau)','(e_b2*e_tau2)');
                Pstrings{i,j} = strrep(Pstrings{i,j},'exp(b3 + 2*tau)','(e_b3*e_tau2)');
                Pstrings{i,j} = strrep(Pstrings{i,j},'exp(b3 + 2*tau_y)','(e_b3*e_tau_y2)');
                
                for n1 = N:-1:1
                    Pstrings{i,j} = strrep(Pstrings{i,j},sprintf('Q_%d',n1),sprintf('Q[%d]', n1-1));
                    Pstrings{i,j} = strrep(Pstrings{i,j},sprintf('S_%d',n1),sprintf('S[%d]', n1-1));
                end

                for n1 = 1:N
                    for n2 = 1:N
                        Pstrings{i,j} = strrep(Pstrings{i,j},sprintf('D_%d_%d^2',n1,n2),sprintf('D_%d_%d*D_%d_%d',n1,n2,n1,n2));
                    end
                end
            end
            s1 = sprintf('P_%d_%d = ',i,j);
            Pstrings{i,j} = [s1, Pstrings{i,j}, ';'];
        end
    end

    Pstrings = Pstrings(P_idx);
    
    for i = 1:length(P_idx)
        s_out = Pstrings{i};
        
        % replace indexes into array with indexes into spare linear
        % array of non-zero elements
        for j = length(P_idx):-1:1
            % index backwards to make sure the big numbers get replaced
            % first
            [k, l] = ind2sub([N N], P_idx(j));
            s1 = sprintf('_%d_%d', k, l);
            s2 = sprintf('[%d]',j-1);
            s_out = strrep(s_out, s1, s2);
        end
        
        nummults=nummults + length(strfind(s_out,'*'));
        numadds=numadds + length(strfind(s_out,'+'));
        numadds=numadds + length(strfind(s_out,'-'));
        
        strings_out{i} = s_out; % for debugging
        fprintf(fid,'\t%s\n',s_out);
    end
    
    fprintf('Multplies: %d Adds: %d\n', nummults, numadds);
    
end

fclose(fid);

%% Create strings for update of x

fid = fopen('autotune_filter_2d.c','a');

for x = {f x_new}
    
    fprintf(fid, '\n\n\n\n\t// X update\n');
    
    Pstrings2=cell(N,1);
    for i=1:N
        Pstrings2{i} = char(x{1}(i));
        Pstrings2{i} = strrep(Pstrings2{i},'Ts^2','Tsq');
        Pstrings2{i} = strrep(Pstrings2{i},'exp(b1)','e_b1');
        Pstrings2{i} = strrep(Pstrings2{i},'exp(b2)','e_b2');
        Pstrings2{i} = strrep(Pstrings2{i},'exp(b3)','e_b3');
        Pstrings2{i} = strrep(Pstrings2{i},'exp(b3d)','e_b3d');
        Pstrings2{i} = strrep(Pstrings2{i},'exp(2*tau)','e_tau*e_tau');
        Pstrings2{i} = strrep(Pstrings2{i},'exp(2*tau_y)','e_tau_y*e_tau_y');
        Pstrings2{i} = strrep(Pstrings2{i},'exp(tau)','e_tau');
        Pstrings2{i} = strrep(Pstrings2{i},'s_a^2','s_a2');
        Pstrings2{i} = strrep(Pstrings2{i},'s_a^3','s_a3');
        Pstrings2{i} = strrep(Pstrings2{i},'(Ts + e_tau)^2','Ts_e_tau2');
        Pstrings2{i} = strrep(Pstrings2{i},'(Ts + e_tau_y)^2','Ts_e_tau_y2');
        Pstrings2{i} = strrep(Pstrings2{i},'S_1','S[0]');
        Pstrings2{i} = strrep(Pstrings2{i},'S_2','S[1]');
        Pstrings2{i} = strrep(Pstrings2{i},'S_3','S[2]');
        
        % replace indexes into array with indexes into spare linear
        % array of non-zero elements
        for j = length(P_idx):-1:1
            % index backwards to make sure the big numbers get replaced
            % first
            [k, l] = ind2sub([N N], P_idx(j));
            s1 = sprintf('_%d_%d', k, l);
            s2 = sprintf('[%d]',j-1);
            Pstrings2{i} = strrep(Pstrings2{i}, s1, s2);
        end
        
        s1 = sprintf('X[%d] = ',i-1);
        Pstrings2{i} = [s1, Pstrings2{i}, ';'];
    end
    
    
    for i=1:N;
        fprintf(fid,'\t%s\n',Pstrings2{i});
    end
        
    nummults=0;
    numadds =0;
    for i=1:N;
        nummults=nummults + length(strfind(Pstrings2{i},'*'));
        numadds=numadds + length(strfind(Pstrings2{i},'+'));
        numadds=numadds + length(strfind(Pstrings2{i},'-'));
    end
    fprintf('Multplies: %d Adds: %d\n', nummults, numadds);
end

fclose(fid);


%%

fid = fopen('autotune_filter_2d.c','a');
fprintf(fid, '\n\n\n\n\t// P initialization\n');
for j = 1:length(P_idx)
    % index backwards to make sure the big numbers get replaced
    % first
    [k, l] = ind2sub([N N], P_idx(j));
    
    if k == l
        s_out = sprintf('P[%d] = q_init[%d];',j-1, k-1);
    else
        s_out = sprintf('P[%d] = 0.0f;',j-1);
    end
    fprintf(fid,'\t%s\n', s_out);
end
fclose(fid)