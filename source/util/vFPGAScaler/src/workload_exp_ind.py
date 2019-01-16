import commands
import os 

serv_0_serv_rate = 1000.0 # VecAdd
serv_1_serv_rate = 100.0 # MatMul
serv_2_serv_rate = 10.0 # Sobel


for y in range(1,4):

  makespan=[]

  for x in range(1,10):
    if (y==1):
      workload_serv_0 = x*0.1
      workload_serv_1 = 0.1
      workload_serv_2 = 0.1
    elif (y==2):
      workload_serv_0 = 0.1
      workload_serv_1 = x*0.1
      workload_serv_2 = 0.1
    elif(y==3):
      workload_serv_0 = 0.1
      workload_serv_1 = 0.1
      workload_serv_2 = x*0.1
   
    serv_0_arrv_rate = serv_0_serv_rate*workload_serv_0
    serv_1_arrv_rate = serv_1_serv_rate*workload_serv_1
    serv_2_arrv_rate = serv_2_serv_rate*workload_serv_2
  
    command = "./workload_exp.sh " + str(serv_0_arrv_rate) + " " + str(serv_1_arrv_rate) + " " + str(serv_2_arrv_rate) + " " + str(x)  
    result = commands.getoutput(command)
    lines = result.split()
    makespan.append( lines[-1] )

    print("Service#", y)
    print(makespan)

  mkdir_command = "mkdir ../log/WorkLoadExpInd_" + str(y)
  os.system(mkdir_command)  
  mv_command = "mv ../log/WorkLoadExp_* ../log/WorkLoadExpInd_" + str(y)
  os.system(mv_command)
