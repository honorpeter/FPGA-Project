


#include "platform.h"
#include "xparameters.h"
#include "xscugic.h"
#include "xaxidma.h"
#include "xmmult_accel_core.h"
#include "lib_xmmult_hw.h"
#include "xil_printf.h"




volatile static int RunExample = 0;
volatile static int ResultExample = 0;

XMmult_accel_core xmmult_dev;

XMmult_accel_core_Config xmmult_config = {
	0,
	XPAR_MMULT_ACCEL_CORE_0_S_AXI_CONTROL_BUS_BASEADDR
};

//Interrupt Controller Instance
XScuGic ScuGic;

// AXI DMA Instance
extern XAxiDma AxiDma;


int XMmultSetup(){
	return XMmult_accel_core_Initialize(&xmmult_dev,0);
}

void XMmultStart(void *InstancePtr){
	XMmult_accel_core *pExample = (XMmult_accel_core *)InstancePtr;
	XMmult_accel_core_InterruptEnable(pExample,1);
	XMmult_accel_core_InterruptGlobalEnable(pExample);
	XMmult_accel_core_Start(pExample);
}


void XMmultIsr(void *InstancePtr){
	XMmult_accel_core *pExample = (XMmult_accel_core *)InstancePtr;

	//Disable the global interrupt
	XMmult_accel_core_InterruptGlobalDisable(pExample);
	//Disable the local interrupt
	XMmult_accel_core_InterruptDisable(pExample,0xffffffff);

	// clear the local interrupt
	XMmult_accel_core_InterruptClear(pExample,1);

	ResultExample = 1;
	// restart the core if it should run again
	if(RunExample){
		XMmultStart(pExample);
	}
}

int XMmultSetupInterrupt()
{
	//This functions sets up the interrupt on the ARM
	int result;
	XScuGic_Config *pCfg = XScuGic_LookupConfig(XPAR_SCUGIC_SINGLE_DEVICE_ID);
	if (pCfg == NULL){
		print("Interrupt Configuration Lookup Failed\n\r");
		return XST_FAILURE;
	}
	result = XScuGic_CfgInitialize(&ScuGic,pCfg,pCfg->CpuBaseAddress);
	if(result != XST_SUCCESS){
		return result;
	}
	// self test
	result = XScuGic_SelfTest(&ScuGic);
	if(result != XST_SUCCESS){
		return result;
	}
	// Initialize the exception handler
	Xil_ExceptionInit();
	// Register the exception handler
	//print("Register the exception handler\n\r");
	Xil_ExceptionRegisterHandler(XIL_EXCEPTION_ID_INT,(Xil_ExceptionHandler)XScuGic_InterruptHandler,&ScuGic);
	//Enable the exception handler
	Xil_ExceptionEnable();
	// Connect the Adder ISR to the exception table
	//print("Connect the Adder ISR to the Exception handler table\n\r");
	result = XScuGic_Connect(&ScuGic,XPAR_FABRIC_MMULT_ACCEL_CORE_0_INTERRUPT_INTR,(Xil_InterruptHandler)XMmultIsr,&xmmult_dev);
	if(result != XST_SUCCESS){
		return result;
	}
	//print("Enable the Adder ISR\n\r");
	XScuGic_Enable(&ScuGic,XPAR_FABRIC_MMULT_ACCEL_CORE_0_INTERRUPT_INTR);
	return XST_SUCCESS;
}

int Setup_HW_Accelerator(float A[INPUT_FM_SIZE], float B[WEIGHTS_SIZE], float res_hw[OUTPUT_FM_SIZE], int dma_size)
//Setup the Vivado HLS Block
{
	int status = XMmultSetup();
	if(status != XST_SUCCESS){
		print("Error: example setup failed\n\r");
		return XST_FAILURE;
	}
	status =  XMmultSetupInterrupt();
	if(status != XST_SUCCESS){
		print("Error: interrupt setup failed\n\r");
		return XST_FAILURE;
	}
	//XMmult_accel_core_SetVal1(&xmmult_dev,val1);
	//XMmult_accel_core_SetVal2(&xmmult_dev,val2);
	XMmultStart(&xmmult_dev);

	//flush the cache
	Xil_DCacheFlushRange((unsigned int)A,INPUT_FM_SIZE*sizeof(float));
	Xil_DCacheFlushRange((unsigned int)B,WEIGHTS_SIZE*sizeof(float));
	Xil_DCacheFlushRange((unsigned int)res_hw,OUTPUT_FM_SIZE*sizeof(float));
	//print("\rCache cleared\n\r");

	return 0;
}


void matrix_multiply_ref(float a[INPUT_FM_SIZE], float b[WEIGHTS_SIZE], float out[OUTPUT_FM_SIZE])
{

	int ia, ib, id;
	for (ib = 0; ib < PATTERN_ROW; ++ib)
	{
		for (ia = 0; ia < BLOCK_TO_ROW_ROW; ++ia)
		{
			float sum = 0;
			for (id = 0; id < PATTERN_COL; ++id)
				sum += a[ia * PATTERN_COL + id] * b[ib*PATTERN_COL+id];
			out[ib * BLOCK_TO_ROW_ROW + ia] = sum;
		}
	}

  // matrix multiplication of a A*B matrix
//	for (ia = 0; ia < BLOCK_TO_ROW_ROW; ++ia)
//		{
//			float sum = 0;
//			for (id = 0; id < PATTERN_COL; ++id)
//				sum += a[ia*PATTERN_COL+id] * b[id];
//			out[ia] = sum;
//		}
		return;


}


//int Run_HW_Accelerator(float A[INPUT_FM_SIZE], float B[WEIGHTS_SIZE], float res_hw[PATTERN_ROW][BLOCK_TO_ROW_ROW], int dma_size)
int Run_HW_Accelerator(float A[INPUT_FM_SIZE], float B[WEIGHTS_SIZE], float res_hw[OUTPUT_FM_SIZE], int dma_size)
{

	//transfer A to the Vivado HLS block
	int status = XAxiDma_SimpleTransfer(&AxiDma, (unsigned int) A, INPUT_FM_SIZE*sizeof(float), XAXIDMA_DMA_TO_DEVICE);
	if (status != XST_SUCCESS) {
		//print("Error: DMA transfer to Vivado HLS block failed\n");
		return XST_FAILURE;
	}
	/* Wait for transfer to be done */
	while (XAxiDma_Busy(&AxiDma, XAXIDMA_DMA_TO_DEVICE)) ;

	//transfer B to the Vivado HLS block
	status = XAxiDma_SimpleTransfer(&AxiDma, (unsigned int) B, WEIGHTS_SIZE*sizeof(float), XAXIDMA_DMA_TO_DEVICE);
	if (status != XST_SUCCESS) {
		//print("Error: DMA transfer to Vivado HLS block failed\n");
		return XST_FAILURE;
	}
	/* Wait for transfer to be done */
	while (XAxiDma_Busy(&AxiDma, XAXIDMA_DMA_TO_DEVICE)) ;


	//get results from the Vivado HLS block
	//while (XAxiDma_Busy(&AxiDma, XAXIDMA_DMA_TO_DEVICE)) ;
	//while (XAxiDma_Busy(&AxiDma, XAXIDMA_DEVICE_TO_DMA));

	status = XAxiDma_SimpleTransfer(&AxiDma, (unsigned int) res_hw, OUTPUT_FM_SIZE*sizeof(float),
			XAXIDMA_DEVICE_TO_DMA);
	if (status != XST_SUCCESS) {
		//print("Error: DMA transfer from Vivado HLS block failed\n");
		return XST_FAILURE;
	}
	/* Wait for transfer to be done */
	while (XAxiDma_Busy(&AxiDma, XAXIDMA_DMA_TO_DEVICE)) ;

	//poll the DMA engine to verify transfers are complete
	/* Waiting for data processing */
	/* While this wait operation, the following action would be done
	 * First: Second matrix will be sent.
	 * After: Multiplication will be compute.
	 * Then: Output matrix will be sent from the accelerator to DDR and
	 * it will be stored at the base address that you set in the first SimpleTransfer
	 */
	while ((XAxiDma_Busy(&AxiDma, XAXIDMA_DEVICE_TO_DMA)) || (XAxiDma_Busy(&AxiDma, XAXIDMA_DMA_TO_DEVICE))) ;

//	while (!ResultExample)
//		print("Waiting for core to finish\n\r");

	return 0;

}
