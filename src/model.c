#include "model.h"
#include "model_struct.h"

#ifdef RAI_TENSORFLOW_BACKEND
#include "backends/tensorflow.h"
#endif /* RAI_TENSORFLOW_BACKEND */

#ifdef RAI_TORCH_BACKEND
#include "backends/torch.h"
#endif /* RAI_TORCH_BACKEND */

#include "utils/alloc.h"
#include "utils/arr_rm_alloc.h"

RedisModuleType *RedisAI_ModelType = NULL;

static void* Model_RdbLoad(struct RedisModuleIO *io, int encver) {
  //todo
  return NULL;
}

static void Model_RdbSave(RedisModuleIO *rdb, void *value) {
  //todo
}

// TODO: pass err in?
static void Model_DTFree(void *value) {
  RAI_Error err = RAI_InitError();
  RAI_ModelFree(value, &err);
  if (err.code != RAI_OK) {
    printf("ERR: %s\n", err.detail);
    RAI_ClearError(&err);
  }
}

int RAI_ModelInit(RedisModuleCtx* ctx) {
  RedisModuleTypeMethods tmModel = {
      .version = REDISMODULE_TYPE_METHOD_VERSION,
      .rdb_load = Model_RdbLoad,
      .rdb_save = Model_RdbSave,
      .aof_rewrite = NULL,
      .mem_usage = NULL,
      .free = Model_DTFree,
      .digest = NULL
  };

  RedisAI_ModelType = RedisModule_CreateDataType(ctx, "AI__MODEL", 0, &tmModel);
  return RedisAI_ModelType != NULL;
}

RAI_Model *RAI_ModelCreate(RAI_Backend backend, RAI_Device device,
                           size_t ninputs, const char **inputs,
                           size_t noutputs, const char **outputs,
                           const char *modeldef, size_t modellen, RAI_Error* err) {
  RAI_Model *model;
  if (backend == RAI_BACKEND_TENSORFLOW) {
    model = RAI_ModelCreateTF(backend, device, ninputs, inputs, noutputs, outputs, modeldef, modellen, err);
  }
  else if (backend == RAI_BACKEND_TORCH) {
    model = RAI_ModelCreateTorch(backend, device, modeldef, modellen, err);
  }
  else {
    RAI_SetError(err, RAI_EUNSUPPORTEDBACKEND, "Unsupported backend.\n");
  }

  return model;
}

void RAI_ModelFree(RAI_Model* model, RAI_Error* err) {
  if (--model->refCount > 0){
    return;
  }

  if (model->backend == RAI_BACKEND_TENSORFLOW) {
    RAI_ModelFreeTF(model, err);
  }
  else if (model->backend == RAI_BACKEND_TORCH) {
    RAI_ModelFreeTorch(model, err);
  }
  else {
    RAI_SetError(err, RAI_EUNSUPPORTEDBACKEND, "Unsupported backend\n");
    assert(0);
  }

  RedisModule_Free(model);
}

RAI_ModelRunCtx* RAI_ModelRunCtxCreate(RAI_Model* model) {
#define PARAM_INITIAL_SIZE 10
  RAI_ModelRunCtx* mctx = RedisModule_Calloc(1, sizeof(*mctx));
  mctx->model = RAI_ModelGetShallowCopy(model);
  mctx->inputs = array_new(RAI_ModelCtxParam, PARAM_INITIAL_SIZE);
  mctx->outputs = array_new(RAI_ModelCtxParam, PARAM_INITIAL_SIZE);
  return mctx;
}

static int Model_RunCtxAddParam(RAI_ModelRunCtx* mctx, RAI_ModelCtxParam* paramArr,
                                const char* name, RAI_Tensor* tensor) {

  RAI_ModelCtxParam param = {
      .name = name,
      .tensor = tensor ? RAI_TensorGetShallowCopy(tensor): NULL,
  };
  paramArr = array_append(paramArr, param);
  return 1;
}

int RAI_ModelRunCtxAddInput(RAI_ModelRunCtx* mctx, const char* inputName, RAI_Tensor* inputTensor) {
  return Model_RunCtxAddParam(mctx, mctx->inputs, inputName, inputTensor);
}

int RAI_ModelRunCtxAddOutput(RAI_ModelRunCtx* mctx, const char* outputName) {
  return Model_RunCtxAddParam(mctx, mctx->outputs, outputName, NULL);
}

size_t RAI_ModelRunCtxNumOutputs(RAI_ModelRunCtx* mctx) {
  return array_len(mctx->outputs);
}

RAI_Tensor* RAI_ModelRunCtxOutputTensor(RAI_ModelRunCtx* mctx, size_t index) {
  assert(RAI_ModelRunCtxNumOutputs(mctx) > index && index >= 0);
  return mctx->outputs[index].tensor;
}

void RAI_ModelRunCtxFree(RAI_ModelRunCtx* mctx) {
  for (size_t i = 0 ; i < array_len(mctx->inputs) ; ++i) {
    RAI_TensorFree(mctx->inputs[i].tensor);
  }
  array_free(mctx->inputs);

  for (size_t i = 0 ; i < array_len(mctx->outputs) ; ++i) {
    if (mctx->outputs[i].tensor) {
      RAI_TensorFree(mctx->outputs[i].tensor);
    }
  }
  array_free(mctx->outputs);

  RAI_Error err = RAI_InitError();
  RAI_ModelFree(mctx->model, &err);

  if (err.code != RAI_OK) {
    // TODO: take it to client somehow
    printf("ERROR!!\n");
    printf("ERR: %s\n", err.detail);
    RAI_ClearError(&err);
  }
}

int RAI_ModelRun(RAI_ModelRunCtx* mctx, RAI_Error* err) {
  int ret;

  switch (mctx->model->backend) {
    case RAI_BACKEND_TENSORFLOW:
      ret = RAI_ModelRunTF(mctx, err);
      break;
    case RAI_BACKEND_TORCH:
      ret = RAI_ModelRunTorch(mctx, err);
      break;
    default:
      RAI_SetError(err, RAI_EUNSUPPORTEDBACKEND, "Unsupported backend.\n");
      break;
  }

  return ret;
}

RAI_Model* RAI_ModelGetShallowCopy(RAI_Model* model) {
  ++model->refCount;
  return model;
}
