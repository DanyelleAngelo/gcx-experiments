import csv
with open('results_bt.csv') as fp:
    reader = csv.reader(fp)
    next(reader)
    for row in reader:
        filename = row[0] + '-bt-extract.csv'
        with open(filename,'w') as ouf:
            print(f'file|algorithm|peak|stack|time|substring_size',file=ouf)
            for i,len in enumerate([1,10,100,1000,10000]):
                print(f'{row[0]}|CBT|{row[8]}|{row[9]}|{float(row[i+3])/100}|{len}',file=ouf)