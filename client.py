import sys
import PyQt5
from pyqtgraph import PlotWidget,plot,mkPen
import os
import xml.etree.ElementTree as xmlET
import socket
import struct
#import ctypes
from enum import Enum
import time #for sleep
from datetime import datetime
import collections
#global settings
oldConnectionsXmlPath="./digiLockPreviousConnections.xml"
TCP_PORT = 4242
ADCBUFFERSIZE = 2**14
TCP_MAX_REC_CHUNK_SIZE = 4096
ADCPRECISION = 10
app=None

class Opmode(Enum):
    operation_mode_scan_cav=40
    operation_mode_scan_lsr=41
    operation_mode_characterise=42
    operation_mode_lock=43
    operation_mode_shutdown=44

class MessageType(Enum):
    getGraph=0
    getGraph_return=1
    setSettings=2
    setSettings_return=3
    getSettings=4
    getSettings_return=5
    getOffset=6
    getOffset_return=7
    
    setOpmode=8
    setOpmode_return=9
    getOpmode=10
    getOpmode_return=11
    
    getCharacterization=12
    getCharacterization_return=13


def connect(Ip):
    print(f"Initializing connection to {Ip}")
    return

class InitialConnectionDialog(PyQt5.QtGui.QDialog):
    
    def __init__(self, *args, **kwargs):
        super(InitialConnectionDialog, self).__init__(*args, **kwargs)
        self.MainWindow=args[0]
        self.setWindowTitle("New Connection")
        
        #upper row for new connection
        self.label_ip = PyQt5.QtGui.QLabel("Server Address: ")
        self.ledit_ip = PyQt5.QtGui.QLineEdit()
        self.ledit_ip.setPlaceholderText("Please enter valid IP")
        
        self.newConnectBtn = PyQt5.QtGui.QPushButton("New Connection", self)
        self.newConnectBtn.clicked.connect(self.on_newConnectBtn_click)
        
        self.upperRow = PyQt5.QtGui.QHBoxLayout()
        self.upperRow.addWidget(self.label_ip)
        self.upperRow.addWidget(self.ledit_ip)
        self.upperRow.addWidget(self.newConnectBtn)
        
        self.label_old = PyQt5.QtGui.QLabel("Previous Connections: ")
        self.oldComboBox = PyQt5.QtWidgets.QComboBox()
        self.oldConnectBtn = PyQt5.QtGui.QPushButton("Use Previous", self)
        self.oldConnectBtn.clicked.connect(self.on_oldConnectBtn_click)
        
        self.lowerRow = PyQt5.QtGui.QHBoxLayout()
        self.lowerRow.addWidget(self.label_old)
        self.lowerRow.addWidget(self.oldComboBox)
        self.getOldIpFromXml()
        self.lowerRow.addWidget(self.oldConnectBtn)
        
        self.globalLayout=PyQt5.QtGui.QVBoxLayout()
        self.globalLayout.addLayout(self.upperRow)
        self.globalLayout.addLayout(self.lowerRow)
        
        self.setLayout(self.globalLayout)
        
    @PyQt5.QtCore.pyqtSlot()
    def getOldIpFromXml(self):
        #fill Combo box with old ip's
        self.oldConXmlTree = None
        if(os.path.isfile(oldConnectionsXmlPath)):
            self.oldConXmlTree = xmlET.parse(oldConnectionsXmlPath)
            root = self.oldConXmlTree.getroot()
            for child in root:
                oldIp=child.get("IP")
                self.oldComboBox.addItem(oldIp)
        return
    
    @PyQt5.QtCore.pyqtSlot()
    def on_newConnectBtn_click(self):
        Ip=self.ledit_ip.text()
        print(f"Adding new Ip ({Ip}) to list of old ips")
        root = None
        if(self.oldConXmlTree == None):
            #need to create xmlDocumentFirst
            root = xmlET.Element("ConnectionList")
        else:
            root = self.oldConXmlTree.getroot()
        newElement=xmlET.Element("PreviousConnection")
        newElement.set("IP", Ip)
        root.append(newElement)
        newTree = xmlET.ElementTree(root)
        newTree.write(oldConnectionsXmlPath)
        self.MainWindow.Ip=Ip
        self.accept()
        
    @PyQt5.QtCore.pyqtSlot()
    def on_oldConnectBtn_click(self):
        Ip=self.oldComboBox.currentText()
        self.MainWindow.Ip=Ip
        self.accept()
     
    def closeEvent(self, evnt):
        evnt.ignore()
        self.reject()
        

class MainWindow(PyQt5.QtWidgets.QMainWindow):
    ledit_settings_list=[]
    scany=list()
    charx=list()
    chary=list()
    def __init__(self, *args, **kwargs):
        super(MainWindow, self).__init__( *args, **kwargs)
        
        
        self.UpperPlotWidget = PlotWidget()
        self.UpperPlotWidget.setBackground("#112233")
        self.UpperPlotWidget.setLabel('left', 'Ch1 [V]')
        self.UpperPlotWidget.setLabel('bottom', 'n-th sample')
        
        self.LowerPlotWidget = PlotWidget()
        self.LowerPlotWidget.setBackground("#112233")
        self.LowerPlotWidget.setLabel('left', 'Offset [V]')
        self.LowerPlotWidget.setLabel('bottom', 'n-th peak')
        
        
        self.UpperCtrlLayout=PyQt5.QtGui.QHBoxLayout()
        self.label_status = PyQt5.QtGui.QLabel("Status:")
        self.ledit_status = PyQt5.QtGui.QLineEdit()
        self.ledit_status.setReadOnly(True)
        self.netindicator_status=PyQt5.QtGui.QProgressBar()
        self.netindicator_status.setMinimum(0)
        self.netindicator_status.setMaximum(100)
        self.netindicator_status.setValue(100)
        self.netindicator_status.setTextVisible(False)
        self.UpperCtrlLayout.addWidget(self.label_status)
        self.UpperCtrlLayout.addWidget(self.ledit_status)
        self.UpperCtrlLayout.addWidget(self.netindicator_status)
        
        self.CtrlTabWidget=PyQt5.QtGui.QTabWidget()
        
        self.OpmodesTabDict=collections.OrderedDict()
        self.OpmodesTabDict["Scan Cav"]=PyQt5.QtGui.QWidget()
        self.OpmodesTabDict["Scan Lsr"]=PyQt5.QtGui.QWidget()
        self.OpmodesTabDict["Characterize"]=PyQt5.QtGui.QWidget()
        self.OpmodesTabDict["Lock"]=PyQt5.QtGui.QWidget()
        self.OpmodesTabDict["Shutdown"]=PyQt5.QtGui.QWidget()
        for key,value in self.OpmodesTabDict.items():
            self.CtrlTabWidget.addTab(value,key)
        self.CtrlTabWidget.currentChanged.connect(self.on_Ctrl_Tab_Change)
        
        

        
        #three alternative LowerCtrl layouts, one for each operation mode

        self.LowerCtrlLOCK=PyQt5.QtGui.QHBoxLayout()
        self.CtrlLock_label_p = PyQt5.QtGui.QLabel("P: ")
        self.CtrlLock_ledit_p = PyQt5.QtGui.QLineEdit()
        self.CtrlLock_ledit_p.setPlaceholderText("Enter a float")
        self.ledit_settings_list.append(self.CtrlLock_ledit_p)
       
        self.CtrlLock_label_i = PyQt5.QtGui.QLabel("I: ")
        self.CtrlLock_ledit_i = PyQt5.QtGui.QLineEdit()
        self.CtrlLock_ledit_i.setPlaceholderText("Enter a float")
        self.ledit_settings_list.append(self.CtrlLock_ledit_i)
        
        self.CtrlLock_label_d = PyQt5.QtGui.QLabel("D: ")
        self.CtrlLock_ledit_d = PyQt5.QtGui.QLineEdit()
        self.CtrlLock_ledit_d.setPlaceholderText("Enter a float")
        self.ledit_settings_list.append(self.CtrlLock_ledit_d)
        
        self.CtrlLock_label_set = PyQt5.QtGui.QLabel("SetP: ")
        self.CtrlLock_ledit_set = PyQt5.QtGui.QLineEdit()
        self.CtrlLock_ledit_set.setPlaceholderText("Enter a float")
        self.ledit_settings_list.append(self.CtrlLock_ledit_set)
        
        self.CtrlLock_send_button = PyQt5.QtGui.QPushButton("Send Values", self)
        self.CtrlLock_send_button.clicked.connect(self.on_newSendBtn_click)
        
        
        
        self.LowerCtrlLOCK.addWidget(self.CtrlLock_label_p)
        self.LowerCtrlLOCK.addWidget(self.CtrlLock_ledit_p)
        self.LowerCtrlLOCK.addWidget(self.CtrlLock_label_i)
        self.LowerCtrlLOCK.addWidget(self.CtrlLock_ledit_i)
        self.LowerCtrlLOCK.addWidget(self.CtrlLock_label_d)
        self.LowerCtrlLOCK.addWidget(self.CtrlLock_ledit_d)
        self.LowerCtrlLOCK.addWidget(self.CtrlLock_label_set)
        self.LowerCtrlLOCK.addWidget(self.CtrlLock_ledit_set)
        self.LowerCtrlLOCK.addWidget(self.CtrlLock_send_button)
        
        self.OpmodesTabDict["Lock"].setLayout(self.LowerCtrlLOCK)
        
        #second possible layout for lowerctrl
        self.LowerCtrlCHAR=PyQt5.QtGui.QHBoxLayout()
        self.CtrlCHAR_dump_button = PyQt5.QtGui.QPushButton("save characterization data", self)
        self.LowerCtrlCHAR.addWidget(self.CtrlCHAR_dump_button)
        self.CtrlCHAR_dump_button.clicked.connect(self.on_CtrlChar_dump_click)

        self.OpmodesTabDict["Characterize"].setLayout(self.LowerCtrlCHAR)
        
        #third possible layout for lowerctrl
        self.LowerCtrlScanLsr=PyQt5.QtGui.QHBoxLayout()
        self.LowerCtrlScanCav=PyQt5.QtGui.QHBoxLayout()
        self.CtrlScanLsr_dump_button = PyQt5.QtGui.QPushButton("save laser scan graph", self)
        self.CtrlScanCav_dump_button = PyQt5.QtGui.QPushButton("save cavity scan graph", self)
        self.CtrlScanLsr_dump_button.clicked.connect(self.on_CtrlScan_dump_click)
        self.CtrlScanCav_dump_button.clicked.connect(self.on_CtrlScan_dump_click)
        self.LowerCtrlScanLsr.addWidget(self.CtrlScanLsr_dump_button)
        self.LowerCtrlScanCav.addWidget(self.CtrlScanCav_dump_button)
        self.OpmodesTabDict["Scan Cav"].setLayout(self.LowerCtrlScanCav)
        self.OpmodesTabDict["Scan Lsr"].setLayout(self.LowerCtrlScanLsr)
                
        #init plot widget
        self.x_range=range(ADCBUFFERSIZE)
        y=[0]*ADCBUFFERSIZE
        
        self.UpperPlot=self.UpperPlotWidget.plot(self.x_range,y,pen=mkPen("#ff2200"))
        self.LowerPlot=self.LowerPlotWidget.plot(self.x_range,y,pen=mkPen("#aaff00"))
        
        
        self.globalLayout=PyQt5.QtGui.QVBoxLayout()
        self.globalLayout.addWidget(self.UpperPlotWidget)
        self.globalLayout.addWidget(self.LowerPlotWidget)
        self.globalLayout.addLayout(self.UpperCtrlLayout)
        self.globalLayout.addWidget(self.CtrlTabWidget)
        
        self.globalWidget = PyQt5.QtGui.QWidget()
        self.globalWidget.setLayout(self.globalLayout)
        
        self.setCentralWidget(self.globalWidget)
        
        dlg=InitialConnectionDialog(self)
        
        #check if dialog is closed with the close button of the window and terminate the app if this is the case
        if(0 == dlg.exec_()):
            exit()
        
        #start network thread
        self.networkingThread_handler = PyQt5.QtCore.QThread()
        self.networkingThread_handler.start()
        
        #place qObject inside this thread
        self.network_thread = NetworkingWorker(self.Ip)
        self.setWindowTitle(f"digitalLock at IP:{self.Ip}")
        self.network_thread.getGraph_return_signal.connect(self.getGraph_return)
        self.network_thread.getSettings_return_signal.connect(self.getSettings_return)
        self.network_thread.getCharacerization_return_signal.connect(self.getCharacerization_return)
        self.network_thread.networkTX_start_signal.connect(self.networkTX_start)
        self.network_thread.networkTX_end_signal.connect(self.networkTX_end)
        #important, do this after the QObject has been moved to the new thread
        self.network_thread.moveToThread(self.networkingThread_handler)
        
        #get initial settings from redpitaya
        self.network_thread.getSettings_signal.emit()
        #WARNING due to thread affinity you must not tamper timers of other threads (call start, stop, etc.)
        #timer to call getGraph repeadietely
        self.graphUpdateTimer=PyQt5.QtCore.QTimer(self)
        self.graphUpdateTimer.setInterval(2000)
        self.graphUpdateTimer.timeout.connect(self.network_thread.getGraph_signal)
        #timer to check for finished characterization
        self.characterizationUpdateTimer=PyQt5.QtCore.QTimer(self)
        self.characterizationUpdateTimer.setInterval(8000)
        self.characterizationUpdateTimer.timeout.connect(self.network_thread.getCharacerization_signal)
        
        self.graphUpdateTimer.start()

    #UI stuff
    @PyQt5.QtCore.pyqtSlot()
    def on_Ctrl_Tab_Change(self):
        tabIdx=self.CtrlTabWidget.currentIndex()
        key,value=list(self.OpmodesTabDict.items())[tabIdx]
        print(f" selected widget {key}")
        if(key=="Scan Lsr"):
            self.network_thread.set_opmode(Opmode.operation_mode_scan_lsr.value)  
        elif(key=="Scan Cav"):
            self.network_thread.set_opmode(Opmode.operation_mode_scan_cav.value)
        elif(key=="Characterize"):
            self.characterizationUpdateTimer.start()
            self.network_thread.set_opmode(Opmode.operation_mode_characterise.value)
        elif(key=="Lock"):
            self.network_thread.set_opmode(Opmode.operation_mode_lock.value)
        else:       #=="shutdown"
            self.network_thread.set_opmode(Opmode.operation_mode_shutdown.value)
        
    @PyQt5.QtCore.pyqtSlot()
    def on_newSendBtn_click(self):
        settings=[]
        for ledit in self.ledit_settings_list:
            settings.append(float(ledit.text()))
        self.network_thread.setSettings_signal.emit(settings)
        
    @PyQt5.QtCore.pyqtSlot()
    def on_CtrlScan_dump_click(self):
        currentdate=datetime.now()
        file=open("./"+currentdate.strftime("%Y_%m_%d")+"__"+currentdate.strftime("%H_%M_%S")+"_scandata.csv", "w")
        for i in range(len(self.scany)):
            file.write(f"{i} {self.scany[i]:1.10f}\n")
        file.close()
    @PyQt5.QtCore.pyqtSlot()
    def on_CtrlChar_dump_click(self):
        currentdate=datetime.now()
        file=open("./"+currentdate.strftime("%Y_%m_%d")+"__"+currentdate.strftime("%H_%M_%S")+"_chardata.csv", "w")
        for i in range(len(self.chary)):
            file.write(f"{self.charx[i]:2.10f} {self.chary[i]:2.10f}\n")
        file.close()
    
    #callbacks from network thread
    @PyQt5.QtCore.pyqtSlot(list,list)
    def getCharacerization_return(self,listx,listy):
        self.characterizationUpdateTimer.stop()
        self.charx=listx
        self.chary=listy
        print(f"got data len x {len(listx)} and len y {len(listy)}")
        self.LowerPlot.setData(listx, listy)
        app.processEvents()    
    @PyQt5.QtCore.pyqtSlot(list)
    def getGraph_return(self,list):
        self.scany=list
        self.UpperPlot.setData(self.x_range, self.scany)
        app.processEvents()
    @PyQt5.QtCore.pyqtSlot(list)
    def getSettings_return(self,list):
        for i in range(len(self.ledit_settings_list)):
            self.ledit_settings_list[i].setText(str(list[i]))
    @PyQt5.QtCore.pyqtSlot()
    def networkTX_start(self):
        self.netindicator_status.setMaximum(100)
        self.netindicator_status.setValue(100)
    @PyQt5.QtCore.pyqtSlot()
    def networkTX_end(self):
        self.netindicator_status.setMaximum(0)
        


#see https://stackoverflow.com/questions/20324804/how-to-use-qthread-correctly-in-pyqt-with-movetothread
class NetworkingWorker(PyQt5.QtCore.QObject):
    mainThread=None
    
    #consumed signals
    getGraph_signal = PyQt5.QtCore.Signal()
    getSettings_signal = PyQt5.QtCore.Signal()
    setSettings_signal = PyQt5.QtCore.Signal(list)
    getOffset_signal = PyQt5.QtCore.Signal()  
    getCharacerization_signal = PyQt5.QtCore.Signal()
    
    #emitted signals
    getGraph_return_signal = PyQt5.QtCore.Signal(list)
    getSettings_return_signal = PyQt5.QtCore.Signal(list)
    getCharacerization_return_signal = PyQt5.QtCore.Signal(list,list)
    
    networkTX_start_signal = PyQt5.QtCore.Signal()
    networkTX_end_signal = PyQt5.QtCore.Signal()
    
    
    
    
    def __init__(self,Ip,*args, **kwargs):
        super(NetworkingWorker, self).__init__()
        
        self.Ip=Ip
        
        self.socket = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        self.socket.connect((self.Ip, TCP_PORT))
       
        self.args = args
        self.kwargs = kwargs
        self.getGraph_signal.connect(self.getGraph)
        self.getSettings_signal.connect(self.getSettings)
        self.setSettings_signal.connect(self.setSettings)
        self.getOffset_signal.connect(self.getOffset)
        self.getCharacerization_signal.connect(self.getCharacerization)
        print("Networking thread initialized")



    #TODO check if int works here
    @PyQt5.QtCore.pyqtSlot(int) 
    def set_opmode(self,opmode):
        self.networkTX_start_signal.emit()
        type=MessageType.setOpmode.value
        message = b''.join([struct.pack("!ii",type,4) , struct.pack("!i",opmode)])
        self.socket.sendall(message)
        #recieve answer
        buffer = self.recvall(8)
        header_tuple = struct.unpack("!ii",buffer)
        requestType = header_tuple[0]
        dataLength = header_tuple[1]
        if(dataLength!=0 or requestType!=MessageType.setOpmode_return.value):
            print(f"got requestType {requestType} should be {MessageType.setOpmode_return.value}, datalength {dataLength}")
            print("Error unexpected network packet after setOpmode")
        self.networkTX_end_signal.emit()

    @PyQt5.QtCore.pyqtSlot()
    def getGraph(self):
        self.networkTX_start_signal.emit()
        #send request
        type=MessageType.getGraph.value
        message = struct.pack("!ii",type,0)
        self.socket.sendall(message)
        #recieve answer
        buffer = self.recvall(8)
        header_tuple = struct.unpack("!ii",buffer)
        requestType = header_tuple[0]
        dataLength = header_tuple[1]
        print(f"expecting {dataLength} bytes or {dataLength/4} floats")
        if(requestType!=MessageType.getGraph_return.value):
            print("Error unexpected network answer")
        buffer = self.recvall(dataLength)
        data=struct.unpack("!"+str(dataLength//4)+"f",buffer)
        graphy=list(data)
        self.networkTX_end_signal.emit()
        self.getGraph_return_signal.emit(graphy)
        
    @PyQt5.QtCore.pyqtSlot()
    def getCharacerization(self):
        self.networkTX_start_signal.emit()
        #send request
        type=MessageType.getCharacterization.value
        message = struct.pack("!ii",type,0)
        self.socket.sendall(message)
        #recieve answer
        buffer = self.recvall(8)
        header_tuple = struct.unpack("!ii",buffer)
        requestType = header_tuple[0]
        dataLength = header_tuple[1]
        if(dataLength==0):
            print("Data not ready yet. Will check again.")
            return
        if(requestType!=MessageType.getCharacterization_return.value):
            print("Error unexpected network answer")
        if(dataLength%8):
            print("Error data not aligned")
            return
        buffer = self.recvall(dataLength//2)
        datax=struct.unpack("!"+str(dataLength//8)+"f",buffer)
        graphx=list(datax)
        buffer = self.recvall(dataLength//2)
        datay=struct.unpack("!"+str(dataLength//8)+"f",buffer)
        graphy=list(datay)
        self.networkTX_end_signal.emit()
        self.getCharacerization_return_signal.emit(graphx,graphy)
    
    @PyQt5.QtCore.pyqtSlot(list)
    def setSettings(self,list):    
        self.networkTX_start_signal.emit()
        #send settings
        type=MessageType.setSettings.value
        message = b''.join([struct.pack("!ii",type,4*len(list)) , struct.pack("!"+str(len(list))+"f",*list)])
        self.socket.sendall(message)
        #recieve answer
        buffer = self.recvall(8)
        header_tuple=struct.unpack("!ii",buffer)
        requestType=header_tuple[0]
        dataLength =header_tuple[1]
        buffer = self.recvall(dataLength)
        if(buffer!=b""):
            print("Error unexpected network packet after setSettings")
        self.networkTX_end_signal.emit()
    
    @PyQt5.QtCore.pyqtSlot()
    def getSettings(self):
        print("getSettings sent")
        self.networkTX_start_signal.emit()
        
        #send request
        type=MessageType.getSettings.value
        message = struct.pack("!ii",type,0)
        self.socket.sendall(message)
        #recieve answer
        buffer = self.recvall(8)
        header_tuple = struct.unpack("!ii",buffer)
        requestType = header_tuple[0]
        dataLength = header_tuple[1]
        buffer = self.recvall(dataLength)
        print("getSettings returned data")

        settingslist = list(struct.unpack("!"+str(dataLength//4)+"f",buffer))
        self.networkTX_end_signal.emit()
        #TODO in main be carefull this is an tuple
        print(settingslist)
        self.getSettings_return_signal.emit(settingslist)
        print("getSettings returned signal")

    @PyQt5.QtCore.pyqtSlot()
    def getOffset(self):
        print("Not implemented yet")
    
    def recvall(self,msg_size):
        arr = bytearray(msg_size)
        pos = 0
        while pos < msg_size:
            if(TCP_MAX_REC_CHUNK_SIZE<msg_size):
                bytesobj = self.socket.recv(TCP_MAX_REC_CHUNK_SIZE)
                actuallyrevievedbytes=len(bytesobj)
                arr[pos:pos+actuallyrevievedbytes]=bytesobj
                pos += actuallyrevievedbytes
            else:
                bytesobj = self.socket.recv(msg_size)
                actuallyrevievedbytes=len(bytesobj)
                arr[pos:pos+actuallyrevievedbytes]=bytesobj
                pos += actuallyrevievedbytes
        return arr

#def main():
app = PyQt5.QtWidgets.QApplication(sys.argv)
window = MainWindow()
window.show()
sys.exit(app.exec_())

#main()

