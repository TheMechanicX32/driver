/**********************************************************************/
/*                                                                    */
/* Program Name: driver - Communicates data between a file system,    */
/*                        and the disk in which it is writing to      */
/* Author:       Ethan Nicholas Huckabee                              */
/* Installation: Pensacola Chrsitian College, Pensacola, Florida      */
/* Course:       CS326, Operating Systems                             */
/* Date Written: April 20, 2022                                       */
/*                                                                    */
/**********************************************************************/

/**********************************************************************/
/*                                                                    */
/* I pledge this assignment is my own first time work.                */
/* I pledge I did not copy or try to copy work from the Internet.     */
/* I pledge I did not copy or try to copy work from any student.      */
/* I pledge I did not copy or try to copy work from any where else.   */
/* I pledge the only person I asked for help from was my teacher.     */
/* I pledge I did not attempt to help any student on this assignment. */
/* I understand if I violate this pledge I will receive a 0 grade.    */
/*                                                                    */
/*                                                                    */
/*                      Signed: _____________________________________ */
/*                                           (signature)              */
/*                                                                    */
/*                                                                    */
/**********************************************************************/

/**********************************************************************/
/*                                                                    */
/* This program simulates communication between a file system and a   */
/* disk. Messages are sent by the file system, copied into the        */
/* pending request list, and then processed by the driver. Location   */
/* of the requests are found, the requests are error checked, and     */
/* processed by the driver. If there are no requests to process, the  */
/* disk motor is turned off, and idle requests are sent to the file   */
/* system until new messages are sent back to the driver.             */
/*                                                                    */
/**********************************************************************/

#include <stdio.h>
#include <stdlib.h>  /* malloc(), free(), exit()                      */

/**********************************************************************/
/*                         Symbolic Constants                         */
/**********************************************************************/
#define TRUE                1    /* Constant true value               */
#define FALSE               0    /* Constant false value              */
#define MIN_REQUEST_NUMBER  1    /* Minimum allowed request number    */
#define MIN_BLOCK_NUMBER    1    /* Minimum allowed block number      */
#define MAX_BLOCK_NUMBER    360  /* Maximum allowed block number      */
#define LIST_HEADER         MIN_BLOCK_NUMBER-1
                                 /* Pending request list header       */
#define LIST_TRAILER        MAX_BLOCK_NUMBER+1
                                 /* Pending request list trailer      */
#define HEADER_ALLOC_ERR    1    /* Can't allocate header memory      */
#define TRAILER_ALLOC_ERR   2    /* Can't allocate trailer memory     */
#define REQUEST_ALLOC_ERR   3    /* Can't allocate request memory     */
#define BYTES_PER_BLOCK     1024 /* Number of bytes per block         */
#define BYTES_PER_SECTOR    512  /* Number of bytes per sector        */
#define CYLINDERS           40   /* Number of cylinders               */
#define SECTORS_PER_TRACK   9    /* Number of sectors per track       */
#define TRACKS_PER_CYLINDER 2    /* Number of tracks per cylinder     */
#define FS_MESSAGE_COUNT    20   /* File system messages array size   */
#define SENSE_CYLINDER      1    /* Get cylinder of the heads code    */
#define SEEK_CYLINDER       2    /* Seek to a cylinder code           */
#define DMA_SETUP           3    /* Set the DMA chip registers code   */
#define START_MOTOR         4    /* Start the disk motor code         */
#define MOTOR_STATUS        5    /* Get status of disk motor code     */
#define READ_DISK           6    /* Read from the disk code           */
#define WRITE_DISK          7    /* Write from the disk code          */
#define STOP_MOTOR          8    /* Stop the disk motor code          */
#define RECALIBRATE         9    /* Recalibrate the disk head code    */
#define OP_CODE_ERROR       -1   /* Invalid operation code error      */
#define REQUEST_NUM_ERROR   -2   /* Invalid request number error      */
#define BLOCK_NUM_ERROR     -4   /* Invalid block number error        */
#define BLOCK_SIZE_ERROR    -8   /* Invalid block size error          */
#define DATA_ADDRESS_ERROR  -16  /* Invalid data address error        */
#define READ_OP_CODE        1    /* Read request operation code       */
#define WRITE_OP_CODE       2    /* Write request operation code      */
#define DMA_SETUP_ERROR     -1   /* Impossible DMA error from disk    */
#define CHECKSUM_ERROR      -2   /* Disk controller checksum failed   */

/**********************************************************************/
/*                         Program Structures                         */
/**********************************************************************/
/* A file system message                                              */
struct message
{
                 int operation_code,  /* Disk operation to perform    */
                     request_number,  /* Unique request number        */
                     block_number,    /* Block number to read/write   */
                     block_size;      /* Block size in bytes          */
   unsigned long int *p_data_address; /* Points to a block in memory  */
};
typedef struct message MESSAGE;

/* A pending request                                                  */
struct request
{
                 int operation_code,  /* Disk operation to perform    */
                     request_number,  /* Unique request number        */
                     block_number,    /* Block number to read/write   */
                     cylinder_number, /* Cylinder number for request  */
                     track_number,    /* Track number for request     */
                     sector_number,   /* Sector number for request    */
                     block_size;      /* Block size in bytes          */
   unsigned long int *p_data_address; /* Points to a block in memory  */
      struct request *p_next_request; /* Points to the next request   */
};
typedef struct request REQUEST;

/**********************************************************************/
/*                        Function Prototypes                         */
/**********************************************************************/
int disk_drive(int code, int arg1, int arg2, int arg3,
               unsigned long int *p_arg4);
   /* Communicates with the disk                                      */
void send_message(MESSAGE *p_fs_message);
   /* Communicates with the file system                               */
REQUEST *create_request_list();
   /* Creates an empty request list with a valid header and trailer   */
int count_pending_requests();
   /* Counts the pending requests in the requests list                */
void copy_messages();
   /* Copies file system messages into to the pending requests list   */
void insert_request(REQUEST *p_request);
   /* Inserts request into the pending request list sorted ascending  */
   /* by block number                                                 */
REQUEST *create_request(MESSAGE fs_request);
   /* Creates a new request from a file system request                */
void convert_block(int block_number, int *p_cylinder, int *p_track,
                   int *p_sector);
   /* Converts file system block number to cylinder, track, and       */
   /* sector numbers                                                  */
REQUEST *get_next_request(int request_cylinder);
   /* Gets the next request using the modified elevator algorithm     */
void remove_request(int request_number);
   /* Removes a request from the pending requests list                */
int power_of_two(int input_value);
   /* Determines if a number is a power of two or not                 */

/**********************************************************************/
/*                         Global Variables                           */
/**********************************************************************/
MESSAGE fs_message[FS_MESSAGE_COUNT]; /* File system messages         */
REQUEST *p_pending_requests;          /* Points to requests list      */

/**********************************************************************/
/*                          Main Function                             */
/**********************************************************************/
int main()
{
   int     bytes_per_cylinder = BYTES_PER_SECTOR * SECTORS_PER_TRACK *
                                TRACKS_PER_CYLINDER,
                                  /* Number of bytes per cylinder     */
           current_cylinder  = 0, /* Cylinder the heads are on        */
           error_code,            /* Tracks errors for every request  */
           checksum,              /* Checksum returned from the disk  */
           idle_counter      = 0, /* Counts the idle requests sent    */
           disk_on           = FALSE;
                                  /* Status of the disk motor         */
   REQUEST *p_request;            /* Points to the current request    */

   /* Create a new pending requests list                              */
   p_pending_requests = create_request_list();

   /* Loop processing file system requests                            */
   while(TRUE)
   {
      /* Loop sending idle messages until file system sends messages  */
      while(count_pending_requests() == 0)
      {
         /* Send an idle message to the file system                   */
         fs_message[0].operation_code = 0;
         fs_message[0].request_number = 0;
         fs_message[0].block_number   = 0;
         fs_message[0].block_size     = 0;
         fs_message[0].p_data_address = NULL;
         send_message(fs_message);

         /* Copy new messages into the pending request list           */
         copy_messages();

         /* Process the idle request if no messages were sent         */
         if (count_pending_requests() == 0)
         {
            /* Increment the idle counter                             */
            idle_counter += 1;

            /* Turn off the disk if there are two idle requests       */
            if (idle_counter >= 1)
            {
               if (disk_on == TRUE)
                  disk_on = disk_drive(STOP_MOTOR,0,0,0,0);
               idle_counter = 0;
            }
         }
      }

      /* Check if the disk is on, and turn it on if it isn't          */
      if (disk_on == FALSE)
      {
         disk_on = disk_drive(START_MOTOR,0,0,0,0);
         disk_drive(MOTOR_STATUS,0,0,0,0);
         current_cylinder = disk_drive(SENSE_CYLINDER,0,0,0,0);
      }
      else
      {
         /* Retrieve the request closest to the heads                 */
         p_request = get_next_request(current_cylinder);

         /* Seek to the cylinder of the current request if the        */
         /* heads are not on the requested cylinder                   */
         if (current_cylinder != p_request->cylinder_number)
         {
            do
            {
               current_cylinder = disk_drive(SEEK_CYLINDER,
                                             p_request->cylinder_number,
                                             0,0,0);
               if (current_cylinder != p_request->cylinder_number)
                  current_cylinder = disk_drive(RECALIBRATE,0,0,0,0);
            }
            while(current_cylinder != p_request->cylinder_number);
         }

         /* Set total error codes to 0                                */
         error_code = 0;

         /* Validate the operation code of the current request        */
         if (p_request->operation_code != READ_OP_CODE &&
             p_request->operation_code != WRITE_OP_CODE)
            error_code += OP_CODE_ERROR;

         /* Validate the request number of the current request        */
         if (p_request->request_number < MIN_REQUEST_NUMBER)
            error_code += REQUEST_NUM_ERROR;

         /* Validate the block number of the current request          */
         if (p_request->block_number < MIN_BLOCK_NUMBER ||
             p_request->block_number > MAX_BLOCK_NUMBER)
            error_code += BLOCK_NUM_ERROR;

         /* Validate the block size of the current request            */
         if (p_request->block_size > bytes_per_cylinder ||
             power_of_two(p_request->block_size) != TRUE)
            error_code += BLOCK_SIZE_ERROR;

         /* Validate the data address of the current request          */
         if (*p_request->p_data_address < 0)
            error_code += DATA_ADDRESS_ERROR;

         /* Prepare to send a completed request to the file system    */
         fs_message[0].operation_code = error_code;
         fs_message[0].request_number = p_request->request_number;
         fs_message[0].block_number   = p_request->block_number;
         fs_message[0].block_size     = p_request->block_size;
         fs_message[0].p_data_address = p_request->p_data_address;

         /* If validation succeeded, read or write to the disk        */
         if (error_code == 0)
         {
            /* Set the DMA chip registers                             */
            if (disk_drive(DMA_SETUP, p_request->sector_number,
                                      p_request->track_number,
                                      p_request->block_size,
                                      p_request->p_data_address)
                == DMA_SETUP_ERROR)
            {
               printf("\nError #%d in create_request_list().",
                      DMA_SETUP_ERROR);
               printf("\nImpossible DMA setup occurred in the disk controller.");
               printf("\nThe program is aborting.");
               exit(DMA_SETUP_ERROR);
            }

            /* Read or write to the disk                              */
            if (p_request->operation_code == READ_OP_CODE)
                do
                {
                   checksum = disk_drive(READ_DISK, 0, 0, 0, 0);
                }
               while(checksum == CHECKSUM_ERROR);
            else if (p_request->operation_code == WRITE_OP_CODE)
               do
               {
                  checksum = disk_drive(WRITE_DISK, 0, 0, 0, 0);
               }
               while(checksum == CHECKSUM_ERROR);
         }

         /* Send the completed request to the file system             */
         send_message  (fs_message);
         copy_messages ();
         remove_request(p_request->request_number);
      }
   }
   return 0;
}

/**********************************************************************/
/*   Creates an empty request list with a valid header and trailer    */
/**********************************************************************/
REQUEST *create_request_list()
{
   REQUEST *p_new_list; /* Points to the newly created request list   */

   /* Get a new request and make it the table header                  */
   if ((p_new_list = (REQUEST *)malloc(sizeof(REQUEST))) == NULL)
   {
      printf("\nError #%d in create_request_list().", HEADER_ALLOC_ERR);
      printf("\nCannot allocate enough memory for the list header.");
      printf("\nThe program is aborting.");
      exit(HEADER_ALLOC_ERR);
   }
   p_new_list->block_number = LIST_HEADER;

   /* Get a new request and attach to end of the list as the trailer  */
   if ((p_new_list->p_next_request = (REQUEST *)malloc(sizeof(REQUEST)))
                                                                == NULL)
   {
      printf("\nError #%d in create_request_list().", TRAILER_ALLOC_ERR);
      printf("\nCannot allocate enough memory for the list trailer");
      printf("\nThe program is aborting.");
      exit(TRAILER_ALLOC_ERR);
   }
   p_new_list->p_next_request->block_number   = LIST_TRAILER;
   p_new_list->p_next_request->p_next_request = NULL;

   /* Return the pointer to the newly created request list            */
   return p_new_list;
}

/**********************************************************************/
/*           Counts the messages sent by the file system              */
/**********************************************************************/
int count_pending_requests()
{
   int     total_messages = 0; /* Message count from the file system  */
   REQUEST *p_request     = p_pending_requests;
                              /* Points to a pending request          */

   /* Traverse the pending requests list counting each request        */
   while (p_request = p_request->p_next_request,
          p_request->block_number != LIST_TRAILER)
      total_messages += 1;
   return total_messages;
}

/**********************************************************************/
/*   Copies file system messages into to the pending requests list    */
/**********************************************************************/
void copy_messages()
{
   int message_count = 0; /* Counts file system messages              */

   /* Loop copying file system messages                               */
   while(message_count < FS_MESSAGE_COUNT &&
         fs_message[message_count].operation_code != 0)
   {
      /* Insert the new request sorting ascending by cylinder         */
      insert_request(create_request(fs_message[message_count]));
      message_count += 1;
   }
   return;
}

/**********************************************************************/
/* Inserts request into the pending request list sorted ascending by  */
/*                           block number                             */
/**********************************************************************/
void insert_request(REQUEST *p_request)
{
   REQUEST *p_current  = p_pending_requests->p_next_request,
                        /* Points to the current request              */
           *p_previous = p_pending_requests;
                        /* Points to the previous request             */

   /* Traverse the pending request to find the insertion point        */
   while(p_current->block_number  <= p_request->block_number &&
         p_current->block_number  != LIST_TRAILER)
   {
      p_previous = p_current;
      p_current  = p_current->p_next_request;
   }

   /* Link the request into the pending requests list                 */
   p_previous->p_next_request = p_request;
   p_request->p_next_request  = p_current;
   return;
}

/**********************************************************************/
/*          Creates a new request from a file system request          */
/**********************************************************************/
REQUEST *create_request(MESSAGE message)
{
   REQUEST *p_new_request; /* Points to a new request                 */

   /* Create a new process                                            */
   if ((p_new_request = (REQUEST *)malloc(sizeof(REQUEST))) == NULL)
   {
      printf("\nError #%d in create_request().", REQUEST_ALLOC_ERR);
      printf("\nCannot allocate enough memory for a new request.");
      printf("\nThe program is aborting.");
      exit(REQUEST_ALLOC_ERR);
   }

   /* Construct the new request from the file system message data     */
   p_new_request->operation_code = message.operation_code;
   p_new_request->request_number = message.request_number;
   p_new_request->block_number   = message.block_number;
   convert_block(p_new_request->block_number,
                &p_new_request->cylinder_number,
                &p_new_request->track_number,
                &p_new_request->sector_number);
   p_new_request->block_size     = message.block_size;
   p_new_request->p_data_address = message.p_data_address;

   /* Return a pointer to the new request                             */
   return p_new_request;
}

/**********************************************************************/
/* Converts file system block numbers to cylinder, track, and sector  */
/*                              numbers                               */
/**********************************************************************/
void convert_block(int block_number, int *p_cylinder, int *p_track,
                   int *p_sector)
{
   /* Calculate the cylinder number                                   */
   *p_cylinder  = (block_number - 1) /  SECTORS_PER_TRACK;

   /* Calculate the track number                                      */
   *p_track     = (block_number - 1) % SECTORS_PER_TRACK;
   *p_track     = *p_track >= SECTORS_PER_TRACK * 0.5f ? 1 : 0;

   /* Calculate the sector number                                     */
   *p_sector    = (block_number - 1) % (SECTORS_PER_TRACK) * 2;
   if (*p_sector > SECTORS_PER_TRACK)
      *p_sector = *p_sector - SECTORS_PER_TRACK;
   return;
}

/**********************************************************************/
/*    Gets the next request using the modified elevator algorithm     */
/**********************************************************************/
REQUEST *get_next_request(int request_cylinder)
{
   REQUEST *p_request = p_pending_requests->p_next_request;
                        /* Points to a request                        */

   /* Get the request closest to the requested cylinder               */
   while(p_request->cylinder_number < request_cylinder &&
         p_request->block_number    != LIST_TRAILER)
      p_request = p_request->p_next_request;

   /* Return the first request if end of the requests list is reached */
   if (p_request->block_number == LIST_TRAILER)
      p_request = p_pending_requests->p_next_request;

   /* Return a pointer to the found request                           */
   return p_request;
}

/**********************************************************************/
/*          Removes a request from the pending requests list          */
/**********************************************************************/
void remove_request(int request_number)
{
   REQUEST *p_current  = p_pending_requests->p_next_request,
                        /* Points to the pending requests list        */
           *p_previous = p_pending_requests;
                        /* Points to the previous request             */

   /* Find the request to be removed from the pending requests list   */
   while(p_current->request_number != request_number &&
         p_current->block_number   != LIST_TRAILER)
   {
      p_previous = p_current;
      p_current  = p_current->p_next_request;
   }

   /* Unlink the found request and free it's memory                   */
   p_previous->p_next_request = p_current->p_next_request;
   free(p_current);
   return;
}

/**********************************************************************/
/*              Determines if a number is a power of two              */
/**********************************************************************/
int power_of_two(int input_value)
{
   while(((input_value % 2) == 0) && input_value > 1)
      input_value = input_value / 2;
   return (input_value == 1);
}
