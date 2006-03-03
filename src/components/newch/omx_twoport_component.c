#include <omxcore.h>

#include <omx_twoport_component.h>


void __attribute__ ((constructor)) omx_twoport_component_register_template() {
  DEBUG(DEB_LEV_SIMPLE_SEQ, "Registering component's template in %s...\n", __func__);	
  
	stComponentType *component = base_component_CreateComponentStruct();
	/** here you can override the base component defaults */
	component->name = "OMX.twoport.component";
  component->constructor = omx_twoport_component_Constructor;
  // port count must be set for the base class constructor (if we call it, and we will)
	component->nports = 2;
  
  register_template(component);
}

OMX_ERRORTYPE omx_twoport_component_Constructor(stComponentType* stComponent) {
	OMX_ERRORTYPE err = OMX_ErrorNone;	

	if (!stComponent->omx_component.pComponentPrivate) {
		stComponent->omx_component.pComponentPrivate = calloc(1, sizeof(omx_twoport_component_PrivateType));
		if(stComponent->omx_component.pComponentPrivate==NULL)
			return OMX_ErrorInsufficientResources;
	}
	
	// we could create our own port structures here
	// fixme maybe the base class could use a "port factory" function pointer?	
	err = base_component_Constructor(stComponent);

	/* here we can override whatever defaults the base_component constructor set
	 * e.g. we can override the function pointers in the private struct  */
	omx_twoport_component_PrivateType* omx_twoport_component_Private = stComponent->omx_component.pComponentPrivate;
	
	// oh well, for the time being, set the port params, now that the ports exist	
	omx_twoport_component_Private->ports[OMX_TWOPORT_INPUTPORT_INDEX]->sPortParam.eDir = OMX_DirInput;
	omx_twoport_component_Private->ports[OMX_TWOPORT_INPUTPORT_INDEX]->sPortParam.nBufferCountActual = 1;
	omx_twoport_component_Private->ports[OMX_TWOPORT_INPUTPORT_INDEX]->sPortParam.nBufferCountMin = 1;
	omx_twoport_component_Private->ports[OMX_TWOPORT_INPUTPORT_INDEX]->sPortParam.nBufferSize = 0;
	omx_twoport_component_Private->ports[OMX_TWOPORT_INPUTPORT_INDEX]->sPortParam.bEnabled = OMX_TRUE;
	omx_twoport_component_Private->ports[OMX_TWOPORT_INPUTPORT_INDEX]->sPortParam.bPopulated = OMX_FALSE;

	omx_twoport_component_Private->ports[OMX_TWOPORT_OUTPUTPORT_INDEX]->sPortParam.eDir = OMX_DirInput;
	omx_twoport_component_Private->ports[OMX_TWOPORT_OUTPUTPORT_INDEX]->sPortParam.nBufferCountActual = 1;
	omx_twoport_component_Private->ports[OMX_TWOPORT_OUTPUTPORT_INDEX]->sPortParam.nBufferCountMin = 1;
	omx_twoport_component_Private->ports[OMX_TWOPORT_OUTPUTPORT_INDEX]->sPortParam.nBufferSize = 0;
	omx_twoport_component_Private->ports[OMX_TWOPORT_OUTPUTPORT_INDEX]->sPortParam.bEnabled = OMX_TRUE;
	omx_twoport_component_Private->ports[OMX_TWOPORT_OUTPUTPORT_INDEX]->sPortParam.bPopulated = OMX_FALSE;
	
	omx_twoport_component_Private->sPortTypesParam.nPorts = 2;
	omx_twoport_component_Private->sPortTypesParam.nStartPortNumber = 0;	
	
	omx_twoport_component_Private->BufferMgmtFunction = omx_twoport_component_BufferMgmtFunction;

	return err;
}

/**
 * This is a straight copypaste from the old reference component, and includes the gotos and all
 * fixme consider refactoring
 */
void* omx_twoport_component_BufferMgmtFunction(void* param) {
	stComponentType* stComponent = (stComponentType*)param;
	OMX_COMPONENTTYPE* pHandle = &stComponent->omx_component;
	
	omx_twoport_component_PrivateType* omx_twoport_component_Private = stComponent->omx_component.pComponentPrivate;
	tsem_t* pInputSem = omx_twoport_component_Private->ports[OMX_TWOPORT_INPUTPORT_INDEX]->pBufferSem;
	tsem_t* pOutputSem = omx_twoport_component_Private->ports[OMX_TWOPORT_OUTPUTPORT_INDEX]->pBufferSem;
	queue_t* pInputQueue = omx_twoport_component_Private->ports[OMX_TWOPORT_INPUTPORT_INDEX]->pBufferQueue;
	queue_t* pOutputQueue = omx_twoport_component_Private->ports[OMX_TWOPORT_OUTPUTPORT_INDEX]->pBufferQueue;
	OMX_BOOL exit_thread = OMX_FALSE;
	OMX_BOOL isInputBufferEnded,flag;
	OMX_BUFFERHEADERTYPE* pOutputBuffer;
	OMX_BUFFERHEADERTYPE* pInputBuffer;
	OMX_U32  nFlags;
	OMX_MARKTYPE *pMark;
	OMX_BOOL *inbufferUnderProcess=&omx_twoport_component_Private->ports[OMX_TWOPORT_INPUTPORT_INDEX]->bBufferUnderProcess;
	OMX_BOOL *outbufferUnderProcess=&omx_twoport_component_Private->ports[OMX_TWOPORT_OUTPUTPORT_INDEX]->bBufferUnderProcess;
	pthread_mutex_t *pInmutex=&omx_twoport_component_Private->ports[OMX_TWOPORT_INPUTPORT_INDEX]->mutex;
	pthread_mutex_t *pOutmutex=&omx_twoport_component_Private->ports[OMX_TWOPORT_OUTPUTPORT_INDEX]->mutex;
	OMX_COMPONENTTYPE* target_component;
	pthread_mutex_t* executingMutex = &omx_twoport_component_Private->executingMutex;
	pthread_cond_t* executingCondition = &omx_twoport_component_Private->executingCondition;
	

	DEBUG(DEB_LEV_FULL_SEQ, "In %s \n", __func__);
	while(stComponent->state == OMX_StateIdle || stComponent->state == OMX_StateExecuting || 
				((omx_twoport_component_Private->ports[OMX_TWOPORT_INPUTPORT_INDEX]->transientState = OMX_StateIdle) && (omx_twoport_component_Private->ports[OMX_TWOPORT_OUTPUTPORT_INDEX]->transientState = OMX_StateIdle))){

		DEBUG(DEB_LEV_SIMPLE_SEQ, "Waiting for input buffer semval=%d \n",pInputSem->semval);
		tsem_down(pInputSem);
		DEBUG(DEB_LEV_FULL_SEQ, "Input buffer arrived\n");
		pthread_mutex_lock(&omx_twoport_component_Private->exit_mutex);
		exit_thread = omx_twoport_component_Private->bExit_buffer_thread;
		pthread_mutex_unlock(&omx_twoport_component_Private->exit_mutex);
		if(exit_thread == OMX_TRUE) {
			DEBUG(DEB_LEV_FULL_SEQ, "Exiting from exec thread...\n");
			break;
		}

		pthread_mutex_lock(pInmutex);
		*inbufferUnderProcess = OMX_TRUE;
		pthread_mutex_unlock(pInmutex);

		pInputBuffer = dequeue(pInputQueue);
		if(pInputBuffer == NULL){
			DEBUG(DEB_LEV_ERR, "What the hell!! had NULL input buffer!!\n");
			reference_Panic();
		}
		nFlags=pInputBuffer->nFlags;
		if(nFlags==OMX_BUFFERFLAG_EOS) {
			DEBUG(DEB_LEV_SIMPLE_SEQ, "Detected EOS flags in input buffer\n");
		}
		if(omx_twoport_component_Private->ports[OMX_TWOPORT_INPUTPORT_INDEX]->bIsPortFlushed==OMX_TRUE) {
			/*Return Input Buffer*/
			goto LOOP1;
		}
		DEBUG(DEB_LEV_FULL_SEQ, "Waiting for output buffer\n");
		tsem_down(pOutputSem);
		pthread_mutex_lock(pOutmutex);
		*outbufferUnderProcess = OMX_TRUE;
		pthread_mutex_unlock(pOutmutex);
		DEBUG(DEB_LEV_FULL_SEQ, "Output buffer arrived\n");
		if(omx_twoport_component_Private->ports[OMX_TWOPORT_INPUTPORT_INDEX]->bIsPortFlushed==OMX_TRUE) {
			/*Return Input Buffer*/
			goto LOOP1;
		}

		pthread_mutex_lock(&omx_twoport_component_Private->exit_mutex);
		exit_thread=omx_twoport_component_Private->bExit_buffer_thread;
		pthread_mutex_unlock(&omx_twoport_component_Private->exit_mutex);
		
		if(exit_thread==OMX_TRUE) {
			DEBUG(DEB_LEV_FULL_SEQ, "Exiting from exec thread...\n");
			pthread_mutex_lock(pOutmutex);
			*outbufferUnderProcess = OMX_FALSE;
			pthread_mutex_unlock(pOutmutex);
			break;
		}
		isInputBufferEnded = OMX_FALSE;
		
		while(!isInputBufferEnded) {
		/**  This condition becomes true when the input buffer has completely be consumed.
			*  In this case is immediately switched because there is no real buffer consumption */
			isInputBufferEnded = OMX_TRUE;
			/* Get a new buffer from the output queue */
			pOutputBuffer = dequeue(pOutputQueue);
			if(pOutputBuffer == NULL){
				DEBUG(DEB_LEV_ERR, "What the hell!! had NULL output buffer!!\n");
				reference_Panic();
			}

			if(omx_twoport_component_Private->ports[OMX_TWOPORT_OUTPUTPORT_INDEX]->bIsPortFlushed==OMX_TRUE) {
				goto LOOP;
			}
			
			if(omx_twoport_component_Private->pMark!=NULL){
				pOutputBuffer->hMarkTargetComponent=omx_twoport_component_Private->pMark->hMarkTargetComponent;
				pOutputBuffer->pMarkData=omx_twoport_component_Private->pMark->pMarkData;
				omx_twoport_component_Private->pMark=NULL;
			}
			target_component=(OMX_COMPONENTTYPE*)pInputBuffer->hMarkTargetComponent;
			if(target_component==(OMX_COMPONENTTYPE *)&stComponent->omx_component) {
				/*Clear the mark and generate an event*/
				(*(stComponent->callbacks->EventHandler))
					(pHandle,
						stComponent->callbackData,
						OMX_EventMark, /* The command was completed */
						1, /* The commands was a OMX_CommandStateSet */
						0, /* The state has been changed in message->messageParam2 */
						pInputBuffer->pMarkData);
			} else if(pInputBuffer->hMarkTargetComponent!=NULL){
				/*If this is not the target component then pass the mark*/
				pOutputBuffer->hMarkTargetComponent	= pInputBuffer->hMarkTargetComponent;
				pOutputBuffer->pMarkData				= pInputBuffer->pMarkData;
			}
			if(nFlags==OMX_BUFFERFLAG_EOS) {
				pOutputBuffer->nFlags=nFlags;
				(*(stComponent->callbacks->EventHandler))
					(pHandle,
						stComponent->callbackData,
						OMX_EventBufferFlag, /* The command was completed */
						1, /* The commands was a OMX_CommandStateSet */
						nFlags, /* The state has been changed in message->messageParam2 */
						NULL);
			}
			
			DEBUG(DEB_LEV_FULL_SEQ, "In %s: got some buffers to fill on output port\n", __func__);

			/* This calls the actual algorithm; fp must be set */			
			if (omx_twoport_component_Private->BufferMgmtCallback) {
				(*(omx_twoport_component_Private->BufferMgmtCallback))(stComponent, pInputBuffer, pOutputBuffer);
			}

			/*Wait if state is pause*/
			if(stComponent->state==OMX_StatePause) {
				if(omx_twoport_component_Private->ports[OMX_TWOPORT_OUTPUTPORT_INDEX]->bWaitingFlushSem!=OMX_TRUE) {
				pthread_cond_wait(executingCondition,executingMutex);
				}
			}

			/*Call ETB in case port is enabled*/
LOOP:
			if (omx_twoport_component_Private->ports[OMX_TWOPORT_OUTPUTPORT_INDEX]->nTunnelFlags & TUNNEL_ESTABLISHED) {
				if(omx_twoport_component_Private->ports[OMX_TWOPORT_OUTPUTPORT_INDEX]->sPortParam.bEnabled==OMX_TRUE){
					OMX_EmptyThisBuffer(omx_twoport_component_Private->ports[OMX_TWOPORT_OUTPUTPORT_INDEX]->hTunneledComponent, pOutputBuffer);
				}
				else { /*Port Disabled then call ETB if port is not the supplier else dont call*/
					if(!(omx_twoport_component_Private->ports[OMX_TWOPORT_OUTPUTPORT_INDEX]->nTunnelFlags & TUNNEL_IS_SUPPLIER))
						OMX_EmptyThisBuffer(omx_twoport_component_Private->ports[OMX_TWOPORT_OUTPUTPORT_INDEX]->hTunneledComponent, pOutputBuffer);
				}
			} else {
				(*(stComponent->callbacks->FillBufferDone))
					(pHandle, stComponent->callbackData, pOutputBuffer);
			}

			pthread_mutex_lock(pOutmutex);
			*outbufferUnderProcess = OMX_FALSE;
			flag=omx_twoport_component_Private->ports[OMX_TWOPORT_OUTPUTPORT_INDEX]->bWaitingFlushSem;
			pthread_mutex_unlock(pOutmutex);
			if ((omx_twoport_component_Private->ports[OMX_TWOPORT_OUTPUTPORT_INDEX]->nTunnelFlags & TUNNEL_ESTABLISHED) && 
				(omx_twoport_component_Private->ports[OMX_TWOPORT_OUTPUTPORT_INDEX]->nTunnelFlags & TUNNEL_IS_SUPPLIER)) {
				if(pOutputSem->semval==omx_twoport_component_Private->ports[OMX_TWOPORT_OUTPUTPORT_INDEX]->nNumTunnelBuffer)
					if(flag==OMX_TRUE) {
						tsem_up(omx_twoport_component_Private->ports[OMX_TWOPORT_OUTPUTPORT_INDEX]->pFlushSem);
					}
			}else{
				if(flag==OMX_TRUE) {
				tsem_up(omx_twoport_component_Private->ports[OMX_TWOPORT_OUTPUTPORT_INDEX]->pFlushSem);
				}
			}
			omx_twoport_component_Private->outbuffercb++;
		}
		/*Wait if state is pause*/
		if(stComponent->state==OMX_StatePause) {
			if(omx_twoport_component_Private->ports[OMX_TWOPORT_INPUTPORT_INDEX]->bWaitingFlushSem!=OMX_TRUE) {
				pthread_cond_wait(executingCondition,executingMutex);
			}
		}

LOOP1:
		if (omx_twoport_component_Private->ports[OMX_TWOPORT_INPUTPORT_INDEX]->nTunnelFlags & TUNNEL_ESTABLISHED) {
			/*Call FTB in case port is enabled*/
			if(omx_twoport_component_Private->ports[OMX_TWOPORT_INPUTPORT_INDEX]->sPortParam.bEnabled==OMX_TRUE){
				OMX_FillThisBuffer(omx_twoport_component_Private->ports[OMX_TWOPORT_INPUTPORT_INDEX]->hTunneledComponent,pInputBuffer);
				omx_twoport_component_Private->inbuffercb++;
			}
			else { /*Port Disabled then call FTB if port is not the supplier else dont call*/
				if(!(omx_twoport_component_Private->ports[OMX_TWOPORT_INPUTPORT_INDEX]->nTunnelFlags & TUNNEL_IS_SUPPLIER)) {
					 OMX_FillThisBuffer(omx_twoport_component_Private->ports[OMX_TWOPORT_INPUTPORT_INDEX]->hTunneledComponent,pInputBuffer);
					 omx_twoport_component_Private->inbuffercb++;
				}
			}
		} else {
			(*(stComponent->callbacks->EmptyBufferDone))
				(pHandle, stComponent->callbackData,pInputBuffer);
			omx_twoport_component_Private->inbuffercb++;
		}
		pthread_mutex_lock(pInmutex);
		*inbufferUnderProcess = OMX_FALSE;
		flag=omx_twoport_component_Private->ports[OMX_TWOPORT_INPUTPORT_INDEX]->bWaitingFlushSem;
		pthread_mutex_unlock(pInmutex);
		if ((omx_twoport_component_Private->ports[OMX_TWOPORT_INPUTPORT_INDEX]->nTunnelFlags & TUNNEL_ESTABLISHED) && 
			(omx_twoport_component_Private->ports[OMX_TWOPORT_INPUTPORT_INDEX]->nTunnelFlags & TUNNEL_IS_SUPPLIER)) {
			if(pInputSem->semval==omx_twoport_component_Private->ports[OMX_TWOPORT_INPUTPORT_INDEX]->nNumTunnelBuffer)
				if(flag==OMX_TRUE) {
					tsem_up(omx_twoport_component_Private->ports[OMX_TWOPORT_INPUTPORT_INDEX]->pFlushSem);
				}
		}else{
			if(flag==OMX_TRUE) {
			tsem_up(omx_twoport_component_Private->ports[OMX_TWOPORT_INPUTPORT_INDEX]->pFlushSem);
			}
		}
		pthread_mutex_lock(&omx_twoport_component_Private->exit_mutex);
		exit_thread=omx_twoport_component_Private->bExit_buffer_thread;
		pthread_mutex_unlock(&omx_twoport_component_Private->exit_mutex);
		
LOOP2:
		if(exit_thread == OMX_TRUE) {
			DEBUG(DEB_LEV_FULL_SEQ, "Exiting from exec thread...\n");
			break;
		}
		continue;
	}
	DEBUG(DEB_LEV_SIMPLE_SEQ,"Exiting Buffer Management Thread\n");
	return NULL;
}
