import commands

serv_0_serv_rate = 1000.0 # VecAdd
serv_1_serv_rate = 100.0 # MatMul
serv_2_serv_rate = 10.0 # Sobel

makespan = []

for x in range(1,10):
  workload = x*0.1
  serv_0_arrv_rate = serv_0_serv_rate*workload
  serv_1_arrv_rate = serv_1_serv_rate*workload
  serv_2_arrv_rate = serv_2_serv_rate*workload

  command = "./workload_exp.sh " + str(serv_0_arrv_rate) + " " + str(serv_1_arrv_rate) + " " + str(serv_2_arrv_rate) + " " + str(workload) 
  result = commands.getoutput(command)
  lines = result.split()
  makespan.append( lines[-1] )


print(makespan)

