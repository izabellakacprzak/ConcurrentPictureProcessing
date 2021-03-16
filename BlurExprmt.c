#include <stdio.h>
#include <time.h>
#include <string.h>
#include <pthread.h>
#include "Utils.h"
#include "Picture.h"
#include "PicProcess.h"

#define BLUR_REGION_SIZE 9
#define NUM_OF_SECTORS 4
#define NUM_OF_THREADS 100
#define EXEC_TIMES 5

/* A queue of blur_args which keeps all jobs to be done */
struct jobqueue{
  pthread_mutex_t job_lock;	/* Lock for synchronizing access */
  struct blur_args *front; 	/* A pointer to the front of the queue */
  int len;			/* The length of the queue */
};

/* Structure representing a pixel to be blurred */
struct blur_args{
  struct picture *pic;		/* A pointer to the picture to blur */
  int startX;			/* The leftmost x coordinate of the section to blur */
  int startY;			/* The topmost y coordinate of the section to blur */
  int width;			/* The width of the section to blur */
  int height;			/* The height of the section to blur */
  struct blur_args *next;	/* A pointer to the next section to blur in job_queue
				   used for pixel-by-pixel blurring */
};

static void print_finish_time(struct timespec start);
static void execute(void (*func) (struct picture *), struct picture *pic, const char *target_file);
static void blur_seq(struct picture *pic);
static void blur_by_column(struct picture *pic);
static void blur_by_row(struct picture *pic);
static void blur_by_sector(struct picture *pic);
static void blur_by_pixel(struct picture *pic);
static void *blur_picture_section(void *args_p);
static void init_queue(struct jobqueue *job_queue);
static void push_job(struct blur_args *args, struct jobqueue *job_queue);
static struct blur_args *pop_job(struct jobqueue *job_queue);
static void initialize_args(struct blur_args *args, struct picture *pic, struct jobqueue *job_queue);
static void* thread_work(void *args_p);

// ---------- MAIN PROGRAM ---------- \\

  int main(int argc, char **argv){

    const char *filename = "images/lego.jpg";
    const char *target_file = "images/lego_blur.jpg";

    struct picture pic;
    if(!init_picture_from_file(&pic, filename)){
      exit(IO_ERROR);
    }
 
    printf("Executing sequential blurring\n");
    execute(blur_seq, &pic, target_file);

    printf("Executing column-by-column blurring\n");
    execute(blur_by_column, &pic, target_file);

    printf("Executing row-by-row blurring\n");
    execute(blur_by_row, &pic, target_file);

    printf("Executing sector blurring\n");
    execute(blur_by_sector, &pic, target_file);

    printf("Executing pixel-by-pixel blurring\n");
    execute(blur_by_pixel, &pic, target_file);

    clear_picture(&pic);
    return 0;
  }

  /* Notes the starting time and executes the blurring EXEC_TIMES times
     then notes the finish time and prints it */
  static void execute(void (*func) (struct picture *), struct picture *pic, const char *target_file){
    struct timespec start;
    clock_gettime(CLOCK_MONOTONIC, &start);
    for(int i = 0; i < EXEC_TIMES; i++){
      func(pic);
    }
    printf("Picture blurring complete\n");
    print_finish_time(start);
    save_picture_to_file(pic, target_file);
   }

  /* Prints the time passed since the start of blurring */
  static void print_finish_time(struct timespec start) {
    double elapsed;
    struct timespec finish;
    clock_gettime(CLOCK_MONOTONIC, &finish);
    elapsed = (finish.tv_sec - start.tv_sec);
    elapsed += (finish.tv_nsec - start.tv_nsec) / 1000000000.0;
    elapsed /= EXEC_TIMES;
    printf("Time passed: %f\n\n", elapsed);
  }

  /* Sequential blurring using one thread only */
  static void blur_seq(struct picture *pic) {
    struct blur_args args;
    args.pic = pic;
    args.startX = 1;
    args.startY = 1;
    args.width = pic->width;
    args.height = pic->height;
    
    pthread_t thread;

    pthread_create(&thread, NULL, blur_picture_section, &args);
    pthread_join(thread, NULL);
  }

  /* Column-by-column blurring
     Uses one thread per column */
  static void blur_by_column(struct picture *pic){
    pthread_t threads[pic->width - 2];
    
    struct blur_args args;
    args.pic = pic;
    args.width = 1;
    args.height = pic->height;
    args.startY = 1;

    for(int i = 0; i < pic->width - 2; i++) {
      args.startX = i + 1;
      pthread_create(&threads[i], NULL, blur_picture_section, &args);
    }

    for(int i = 0; i< pic->width - 2; i++) {
      pthread_join(threads[i], NULL);
    }
  }

  /* Row-by-row blurring
     Uses one thread per row */
  static void blur_by_row(struct picture *pic){
    pthread_t threads[pic->height - 2];
    
    struct blur_args args;
    args.pic = pic;
    args.width = pic->width;
    args.height = 1;
    args.startX = 1;

    for(int i = 0; i < pic->height - 2; i++) {
      args.startY = i + 1;
      pthread_create(&threads[i], NULL, blur_picture_section, &args);
    }

    for(int i = 0; i< pic->height - 2; i++) {
      pthread_join(threads[i], NULL);
    }
  }

  /* Helper funtion to initialize an array of arguments
     of size equal to the number of pixels in pic (without the borders)
     Used in pixel-by-pixel blurring */
  static void initialize_args(struct blur_args *args, struct picture *pic,
		  struct jobqueue *job_queue){
    int i = 0;
    for(int k = 1; k < pic->height - 1; k++){
      for(int j = 1; j < pic->width - 1; j++){
        args[i].pic = pic;
        args[i].width = 1;
        args[i].height = 1;
        args[i].startX = j;
	args[i].startY = k;
	args[i].next = NULL;
	push_job(&args[i], job_queue);
	i++;
      }
    }
  }

  /* Initializes an empty queue */
  static void init_queue(struct jobqueue *job_queue){
    pthread_mutex_init(&job_queue->job_lock, NULL);
    job_queue->front = NULL;
    job_queue->len = 0;
  }

  /* Pushes a new job at the front of the queue */
  static void push_job(struct blur_args *args, struct jobqueue *job_queue){
    if(job_queue->len == 0){
      job_queue->front = args;
    } else {
      args->next = job_queue->front;
      job_queue->front = args;
    } 
    job_queue->len++;
  }

  /* Returns a job at the front of the queue
     if empty returns NULL */
  static struct blur_args *pop_job(struct jobqueue *job_queue){
    if(job_queue->len == 0){
      return NULL;
    }
    struct blur_args *args = job_queue->front;
    job_queue->front = args->next;
    args->next = NULL;
    job_queue->len--;
    return args;
  }

  /* Repeatedly pops a job from the job_queue
     and runs blur_picture_section until there are
     no more jobs */
  static void* thread_work(void *args_p){
    struct jobqueue *job_queue = (struct jobqueue *) args_p;
    struct blur_args *args;
    bool done = false;
    pthread_mutex_t *lock = (pthread_mutex_t *) &job_queue->job_lock;
    while(!done){
      pthread_mutex_lock(lock);
      args = pop_job(job_queue);
      pthread_mutex_unlock(lock);
      if(args == NULL){
        done = true;
      } else {
        blur_picture_section(args);
      }
    }
  }

  /* Pixel-by-pixel blurring
     Uses one thread per pixel
     Repeatedly creates NUM_OF_THREADS threads and joins them
     until all pixels are blurred */
  static void blur_by_pixel(struct picture *pic){
    pthread_t *threads = malloc(NUM_OF_THREADS * sizeof(pthread_t));
    struct jobqueue job_queue;
    init_queue(&job_queue);

    struct blur_args *args = malloc((pic->width - 2) * (pic->height - 2)
		    * sizeof(struct blur_args));
    initialize_args(args, pic, &job_queue);

    int thread_count = 0;
    for(int i = 0; i < NUM_OF_THREADS; i++){ 
      
      pthread_create(&threads[i],
                  NULL, thread_work, &job_queue);
      thread_count++;
    }

    for(int i = 0; i < NUM_OF_THREADS; i++){
      pthread_join(threads[i], NULL);
    }

    free(args);
    free(threads);
  }

  /* Sector blurring
     divides the picture into 4 sections
     if there are less than 4 rows uses blur_seq */
  static void blur_by_sector(struct picture *pic){
    pthread_t threads[NUM_OF_SECTORS];
    int sectors_height = (pic->height - 2) / NUM_OF_SECTORS;
    int mod = (pic->height - 2) % NUM_OF_SECTORS;
    
    if(sectors_height == 0) {
      blur_seq(pic);
      return;
    }

    struct blur_args args1;
    struct blur_args args2;
    struct blur_args args3;
    struct blur_args args4;

    args1.pic = pic;
    args1.width = pic->width;
    args1.height = sectors_height;
    args1.startX = 1;
    args1.startY = sectors_height * 0 + 1;

    args2.pic = pic;
    args2.width = pic->width;
    args2.height = sectors_height;
    args2.startX = 1;
    args2.startY = sectors_height * 1 + 1;

    args3.pic = pic;
    args3.width = pic->width;
    args3.height = sectors_height;
    args3.startX = 1;
    args3.startY = sectors_height * 2 + 1;

    args4.pic = pic;
    args4.width = pic->width; 
    args4.height = sectors_height + mod;
    args4.startX = 1;
    args4.startY = sectors_height * 3 + 1;

    pthread_create(&threads[0], NULL, blur_picture_section, &args1);
    pthread_create(&threads[1], NULL, blur_picture_section, &args2);
    pthread_create(&threads[2], NULL, blur_picture_section, &args3);
    pthread_create(&threads[3], NULL, blur_picture_section, &args4);

    for(int i = 0; i < NUM_OF_SECTORS; i++){
      pthread_join(threads[i], NULL);
    }
  }

  /* Blurs the section provided in args_p struct
     which is a struct blur_args */
  static void *blur_picture_section(void *args_p){
    struct blur_args *args = (struct blur_args *) args_p;
    struct picture *pic = (struct picture *) args->pic;
    int startX = (int) args->startX;
    int startY = (int) args->startY;
    int width = (int) args->width;
    int height = (int) args->height;

    struct picture tmp;
    tmp.img = copy_image(pic->img);
    tmp.width = pic->width;
    tmp.height = pic->height;

    for(int i = startX ; i < width + startX; i++){
      for(int j = startY ; j < height + startY; j++){

        struct pixel rgb;
        int sum_red = 0;
        int sum_green = 0;
        int sum_blue = 0;

        for(int n = -1; n <= 1; n++){
          for(int m = -1; m <= 1; m++){
            rgb = get_pixel(&tmp, i+n, j+m);
            sum_red += rgb.red;
            sum_green += rgb.green;
            sum_blue += rgb.blue;
          }
        }

        rgb.red = sum_red / BLUR_REGION_SIZE;
        rgb.green = sum_green / BLUR_REGION_SIZE;
        rgb.blue = sum_blue / BLUR_REGION_SIZE;

        set_pixel(pic, i, j, &rgb);
      }
    }
    clear_picture(&tmp);
  }
