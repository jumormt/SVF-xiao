#include <pthread.h>

int shared;

void *worker(void *arg) {
  shared = 1;
  return arg;
}

int main(void) {
  pthread_t tid;
  pthread_create(&tid, 0, worker, 0);
  shared = 2;
  pthread_join(tid, 0);
  shared = 3;
  return shared;
}
