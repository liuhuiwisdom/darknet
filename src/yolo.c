#include "network.h"
#include "detection_layer.h"
#include "cost_layer.h"
#include "utils.h"
#include "parser.h"
#include "box.h"
#include "option_list.h"

#ifdef OPENCV
#include "opencv2/highgui/highgui_c.h"
#endif

#define CLASSES_NUM (20)

char *voc_names[] = {"aeroplane", "bicycle", "bird", "boat", "bottle", "bus", "car", "cat", "chair", "cow", "diningtable", "dog", "horse", "motorbike", "person", "pottedplant", "sheep", "sofa", "train", "tvmonitor"};
image voc_labels[CLASSES_NUM];


void train_yolo(char *datacfg, char *cfgfile, char *weightfile)
{    
    list *options = read_data_cfg(datacfg);
    
    char *train_list = option_find_str(options, "train", "data/train_list.txt");
    //char *test_list = option_find_str(options, "test", "data/test_list.txt");
    //char *valid_list = option_find_str(options, "valid", "data/valid_list.txt");
    
    char *backup_directory = option_find_str(options, "backup", "/backup/");
    //char *label_list = option_find_str(options, "labels", "data/labels_list.txt");
    
    //int classes = option_find_int(options, "classes", 2);
    
    srand(time(0));
    data_seed = time(0);
    char *base = basecfg(cfgfile);
    printf("%s\n", base);
    float avg_loss = -1;
    network net = parse_network_cfg(cfgfile);
    if(weightfile){
        load_weights(&net, weightfile);
    }
    printf("Learning Rate: %g, Momentum: %g, Decay: %g\n", net.learning_rate, net.momentum, net.decay);
    int imgs = net.batch*net.subdivisions;
    int i = *net.seen/imgs;
    data train, buffer;


    layer l = net.layers[net.n - 1];

    int side = l.side;
    int classes = l.classes;
    float jitter = l.jitter;

    list *plist = get_paths(train_list);
    //int N = plist->size;
    char **paths = (char **)list_to_array(plist);

    load_args args = {0};
    args.w = net.w;
    args.h = net.h;
    args.paths = paths;
    args.n = imgs;
    args.m = plist->size;
    args.classes = classes;
    args.jitter = jitter;
    args.num_boxes = side;
    args.d = &buffer;
    args.type = REGION_DATA;

    pthread_t load_thread = load_data_in_thread(args);
    clock_t time;
    //while(i*imgs < N*120){
    while(get_current_batch(net) < net.max_batches){
        i += 1;
        time=clock();
        pthread_join(load_thread, 0);
        train = buffer;
        load_thread = load_data_in_thread(args);

        printf("Loaded: %lf seconds\n", sec(clock()-time));

        time=clock();
        float loss = train_network(net, train);
        if (avg_loss < 0) avg_loss = loss;
        avg_loss = avg_loss*.9 + loss*.1;

        printf("%d: %f, %f avg, %f rate, %lf seconds, %d images\n", i, loss, avg_loss, get_current_rate(net), sec(clock()-time), i*imgs);
        if(i%1000==0 || (i < 1000 && i%100 == 0)){
            char buff[256];
            sprintf(buff, "%s/%s_%06d.weights", backup_directory, base, i);
            save_weights(net, buff);
        }
        free_data(train);
    }
    char buff[256];
    sprintf(buff, "%s/%s_final.weights", backup_directory, base);
    save_weights(net, buff);
}

void convert_yolo_detections(float *predictions, int classes, int num, int square, int side, int w, int h, float thresh, float **probs, box *boxes, int only_objectness)
{
    int i,j,n;
    //int per_cell = 5*num+classes;
    for (i = 0; i < side*side; ++i){
        int row = i / side;
        int col = i % side;
        for(n = 0; n < num; ++n){
            int index = i*num + n;
            int p_index = side*side*classes + i*num + n;
            float scale = predictions[p_index];
            int box_index = side*side*(classes + num) + (i*num + n)*4;
            boxes[index].x = (predictions[box_index + 0] + col) / side * w;
            boxes[index].y = (predictions[box_index + 1] + row) / side * h;
            boxes[index].w = pow(predictions[box_index + 2], (square?2:1)) * w;
            boxes[index].h = pow(predictions[box_index + 3], (square?2:1)) * h;
            for(j = 0; j < classes; ++j){
                int class_index = i*classes;
                float prob = scale*predictions[class_index+j];
                probs[index][j] = (prob > thresh) ? prob : 0;
            }
            if(only_objectness){
                probs[index][0] = scale;
            }
        }
    }
}

void print_yolo_detections(FILE **fps, char *id, box *boxes, float **probs, int total, int classes, int w, int h)
{
    int i, j;
    for(i = 0; i < total; ++i){
        float xmin = boxes[i].x - boxes[i].w/2.;
        float xmax = boxes[i].x + boxes[i].w/2.;
        float ymin = boxes[i].y - boxes[i].h/2.;
        float ymax = boxes[i].y + boxes[i].h/2.;

        if (xmin < 0) xmin = 0;
        if (ymin < 0) ymin = 0;
        if (xmax > w) xmax = w;
        if (ymax > h) ymax = h;

        for(j = 0; j < classes; ++j){
            if (probs[i][j]) fprintf(fps[j], "%s %f %f %f %f %f\n", id, probs[i][j],
                    xmin, ymin, xmax, ymax);
        }
    }
}

void validate_yolo(char *datacfg, char *cfgfile, char *weightfile)
{
    list *options = read_data_cfg(datacfg);
    
    //char *train_list = option_find_str(options, "train", "data/train_list.txt");
    //char *test_list = option_find_str(options, "test", "data/test_list.txt");
    char *valid_list = option_find_str(options, "valid", "data/valid_list.txt");
    
    //char *backup_directory = option_find_str(options, "backup", "/backup/");
    //char *label_list = option_find_str(options, "labels", "data/labels_list.txt");
    
    //int classes = option_find_int(options, "classes", 2);
    
    //char **labels = get_labels(label_list);
    
    network net = parse_network_cfg(cfgfile);
    if(weightfile){
        load_weights(&net, weightfile);
    }
    set_batch_network(&net, 1);
    fprintf(stderr, "Learning Rate: %g, Momentum: %g, Decay: %g\n", net.learning_rate, net.momentum, net.decay);
    srand(time(0));

    char *base = "results/comp4_det_test_";
    list *plist = get_paths(valid_list);
    char **paths = (char **)list_to_array(plist);

    layer l = net.layers[net.n-1];
    int classes = l.classes;
    int square = l.sqrt;
    int side = l.side;

    int j;
    FILE **fps = calloc(classes, sizeof(FILE *));
    for(j = 0; j < classes; ++j){
        char buff[1024];
        snprintf(buff, 1024, "%s%s.txt", base, voc_names[j]);
        fps[j] = fopen(buff, "w");
    }
    box *boxes = calloc(side*side*l.n, sizeof(box));
    float **probs = calloc(side*side*l.n, sizeof(float *));
    for(j = 0; j < side*side*l.n; ++j) probs[j] = calloc(classes, sizeof(float *));

    int m = plist->size;
    int i=0;
    int t;

    float thresh = .001;
    int nms = 1;
    float iou_thresh = .5;

    int nthreads = 2;
    image *val = calloc(nthreads, sizeof(image));
    image *val_resized = calloc(nthreads, sizeof(image));
    image *buf = calloc(nthreads, sizeof(image));
    image *buf_resized = calloc(nthreads, sizeof(image));
    pthread_t *thr = calloc(nthreads, sizeof(pthread_t));

    load_args args = {0};
    args.w = net.w;
    args.h = net.h;
    args.type = IMAGE_DATA;

    for(t = 0; t < nthreads; ++t){
        args.path = paths[i+t];
        args.im = &buf[t];
        args.resized = &buf_resized[t];
        thr[t] = load_data_in_thread(args);
    }
    time_t start = time(0);
    for(i = nthreads; i < m+nthreads; i += nthreads){
        fprintf(stderr, "%d\n", i);
        for(t = 0; t < nthreads && i+t-nthreads < m; ++t){
            pthread_join(thr[t], 0);
            val[t] = buf[t];
            val_resized[t] = buf_resized[t];
        }
        for(t = 0; t < nthreads && i+t < m; ++t){
            args.path = paths[i+t];
            args.im = &buf[t];
            args.resized = &buf_resized[t];
            thr[t] = load_data_in_thread(args);
        }
        for(t = 0; t < nthreads && i+t-nthreads < m; ++t){
            char *path = paths[i+t-nthreads];
            char *id = basecfg(path);
            float *X = val_resized[t].data;
            float *predictions = network_predict(net, X);
            int w = val[t].w;
            int h = val[t].h;
            convert_yolo_detections(predictions, classes, l.n, square, side, w, h, thresh, probs, boxes, 0);
            if (nms) do_nms_sort(boxes, probs, side*side*l.n, classes, iou_thresh);
            print_yolo_detections(fps, id, boxes, probs, side*side*l.n, classes, w, h);
            free(id);
            free_image(val[t]);
            free_image(val_resized[t]);
        }
    }
    fprintf(stderr, "Total Detection Time: %f Seconds\n", (double)(time(0) - start));
}

void validate_yolo_recall(char *datacfg, char *cfgfile, char *weightfile)
{
    list *options = read_data_cfg(datacfg);
    
    //char *train_list = option_find_str(options, "train", "data/train_list.txt");
    char *test_list = option_find_str(options, "test", "data/test_list.txt");
    //char *valid_list = option_find_str(options, "valid", "data/valid_list.txt");
    
    //char *backup_directory = option_find_str(options, "backup", "/backup/");
    //char *label_list = option_find_str(options, "labels", "data/labels_list.txt");
    
    //int classes = option_find_int(options, "classes", 2);
    
    //char **labels = get_labels(label_list);
    
    network net = parse_network_cfg(cfgfile);
    if(weightfile){
        load_weights(&net, weightfile);
    }
    set_batch_network(&net, 1);
    fprintf(stderr, "Learning Rate: %g, Momentum: %g, Decay: %g\n", net.learning_rate, net.momentum, net.decay);
    srand(time(0));

    char *base = "results/comp4_det_test_";
    list *plist = get_paths(test_list);
    char **paths = (char **)list_to_array(plist);

    layer l = net.layers[net.n-1];
    int classes = l.classes;
    int square = l.sqrt;
    int side = l.side;

    int j, k;
    FILE **fps = calloc(classes, sizeof(FILE *));
    for(j = 0; j < classes; ++j){
        char buff[1024];
        snprintf(buff, 1024, "%s%s.txt", base, voc_names[j]);
        fps[j] = fopen(buff, "w");
    }
    box *boxes = calloc(side*side*l.n, sizeof(box));
    float **probs = calloc(side*side*l.n, sizeof(float *));
    for(j = 0; j < side*side*l.n; ++j) probs[j] = calloc(classes, sizeof(float *));

    int m = plist->size;
    int i=0;

    //float thresh = .001;
    float thresh = .2;
    float iou_thresh = .5;
    float nms = 0;

    int total = 0;
    int correct = 0;
    int proposals = 0;
    float avg_iou = 0;

    for(i = 0; i < m; ++i){
        char *path = paths[i];
        image orig = load_image_color(path, 0, 0);
        image sized = resize_image(orig, net.w, net.h);
        char *id = basecfg(path);
        float *predictions = network_predict(net, sized.data);
        convert_yolo_detections(predictions, classes, l.n, square, side, 1, 1, thresh, probs, boxes, 1);
        if (nms) do_nms(boxes, probs, side*side*l.n, 1, nms);

        char *labelpath = find_replace(path, "images", "labels");
        labelpath = find_replace(labelpath, "JPEGImages", "labels");
        labelpath = find_replace(labelpath, ".jpg", ".txt");
        labelpath = find_replace(labelpath, ".JPEG", ".txt");
        labelpath = find_replace(labelpath, ".bmp", ".txt");
        labelpath = find_replace(labelpath, ".dib", ".txt");
        labelpath = find_replace(labelpath, ".jpe", ".txt");
        labelpath = find_replace(labelpath, ".jp2", ".txt");
        labelpath = find_replace(labelpath, ".png", ".txt");
        labelpath = find_replace(labelpath, ".pbm", ".txt");
        labelpath = find_replace(labelpath, ".pgm", ".txt");
        labelpath = find_replace(labelpath, ".ppm", ".txt");
        labelpath = find_replace(labelpath, ".sr", ".txt");
        labelpath = find_replace(labelpath, ".ras", ".txt");
        labelpath = find_replace(labelpath, ".tiff", ".txt");
        labelpath = find_replace(labelpath, ".tif", ".txt");

        int num_labels = 0;
        box_label *truth = read_boxes(labelpath, &num_labels);
        for(k = 0; k < side*side*l.n; ++k){
            if(probs[k][0] > thresh){
                ++proposals;
            }
        }
        for (j = 0; j < num_labels; ++j) {
            ++total;
            box t = {truth[j].x, truth[j].y, truth[j].w, truth[j].h};
            float best_iou = 0;
            for(k = 0; k < side*side*l.n; ++k){
                float iou = box_iou(boxes[k], t);
                if(probs[k][0] > thresh && iou > best_iou){
                    best_iou = iou;
                }
            }
            avg_iou += best_iou;
            if(best_iou > iou_thresh){
                ++correct;
            }
        }

        fprintf(stderr, "%5d %5d %5d\tRPs/Img: %.2f\tIOU: %.2f%%\tRecall:%.2f%%\n", i, correct, total, (float)proposals/(i+1), avg_iou*100/total, 100.*correct/total);
        free(id);
        free_image(orig);
        free_image(sized);
    }
}


void validate_yolo_classify(char *datacfg, char *cfgfile, char *weightfile)
{
    list *options = read_data_cfg(datacfg);
    
    //char *train_list = option_find_str(options, "train", "data/train_list.txt");
    //char *test_list = option_find_str(options, "test", "data/test_list.txt");
    char *valid_list = option_find_str(options, "valid", "data/valid_list.txt");
    
    //char *backup_directory = option_find_str(options, "backup", "/backup/");
    //char *label_list = option_find_str(options, "labels", "data/labels_list.txt");
    
    //int classes = option_find_int(options, "classes", 2);
    
    //char **labels = get_labels(label_list);
    
    network net = parse_network_cfg(cfgfile);
    if(weightfile){
        load_weights(&net, weightfile);
    }
    set_batch_network(&net, 1);
    fprintf(stderr, "Learning Rate: %g, Momentum: %g, Decay: %g\n", net.learning_rate, net.momentum, net.decay);
    srand(time(0));

    char *base = "results/comp4_det_test_";
    //list *plist = get_paths("data/voc.2007.test");
    list *plist = get_paths(valid_list);
    
    char **paths = (char **)list_to_array(plist);

    layer l = net.layers[net.n-1];
    int classes = l.classes;
    int square = l.sqrt;
    int side = l.side;

    int j, k;
    FILE **fps = calloc(classes, sizeof(FILE *));
    for(j = 0; j < classes; ++j){
        char buff[1024];
        snprintf(buff, 1024, "%s%s.txt", base, voc_names[j]);
        fps[j] = fopen(buff, "w");
    }
    box *boxes = calloc(side*side*l.n, sizeof(box));
    float **probs = calloc(side*side*l.n, sizeof(float *));
    for(j = 0; j < side*side*l.n; ++j) probs[j] = calloc(classes, sizeof(float *));

    int m = plist->size;
    int i=0;

    //float thresh = .001;
    float thresh = .2;
    float iou_thresh = .5;
    //float nms = 0;
    float nms = 0.5;
    

    int total = 0;
    int correct = 0;
    int class_correct = 0;
    int proposals = 0;
    float avg_iou = 0;

    for(i = 0; i < m; ++i){
        char *path = paths[i];
        image orig = load_image_color(path, 0, 0);
        image sized = resize_image(orig, net.w, net.h);
        char *id = basecfg(path);
        float *predictions = network_predict(net, sized.data);
        convert_yolo_detections(predictions, classes, l.n, square, side, 1, 1, thresh, probs, boxes, 0);
        //if (nms) do_nms(boxes, probs, side*side*l.n, 1, nms);
        if (nms) do_nms(boxes, probs, side*side*l.n, classes, nms);

        char *labelpath = find_replace(path, "images", "labels");
        labelpath = find_replace(labelpath, "JPEGImages", "labels");
        labelpath = find_replace(labelpath, ".jpg", ".txt");
        labelpath = find_replace(labelpath, ".JPEG", ".txt");
        labelpath = find_replace(labelpath, ".bmp", ".txt");
        labelpath = find_replace(labelpath, ".dib", ".txt");
        labelpath = find_replace(labelpath, ".jpe", ".txt");
        labelpath = find_replace(labelpath, ".jp2", ".txt");
        labelpath = find_replace(labelpath, ".png", ".txt");
        labelpath = find_replace(labelpath, ".pbm", ".txt");
        labelpath = find_replace(labelpath, ".pgm", ".txt");
        labelpath = find_replace(labelpath, ".ppm", ".txt");
        labelpath = find_replace(labelpath, ".sr", ".txt");
        labelpath = find_replace(labelpath, ".ras", ".txt");
        labelpath = find_replace(labelpath, ".tiff", ".txt");
        labelpath = find_replace(labelpath, ".tif", ".txt");

        int num_labels = 0;
        box_label *truth = read_boxes(labelpath, &num_labels);
        for(k = 0; k < side*side*l.n; ++k){
            int class = max_index(probs[k], classes);
            float prob = probs[k][class];
            //fprintf(stderr, "path=%s\t\tk=%d\tprob=%f\tclass=%d\n", path, k, prob, class);
        
            if(prob > thresh){
                ++proposals;
            }
        }
        for (j = 0; j < num_labels; ++j) {
            ++total;
            box t = {truth[j].x, truth[j].y, truth[j].w, truth[j].h};
            float best_iou = 0;
            int pre_class = -1;
            for(k = 0; k < side*side*l.n; ++k){
                float iou = box_iou(boxes[k], t);
                int class = max_index(probs[k], classes);
                float prob = probs[k][class];
                //fprintf(stderr, "path=%s\t\tk=%d\tprob=%f\tclass=%d\n", path, k, prob, class);
                if(prob > thresh && iou > best_iou){
                    best_iou = iou;
                    pre_class = class;
                }
            }
            avg_iou += best_iou;
            
            if(best_iou > iou_thresh){
                ++correct;
            }
            
            if(pre_class == truth[j].id){
                ++class_correct;
            }
            
            //fprintf(stderr, "true_class=%d\tpre_class=%d\n", truth[j].id, pre_class);
        
        }

        fprintf(stderr, "%5d %5d %5d\tRPs/Img: %.2f\tIOU: %.2f%%\tRecall:%.2f%%\t\tClassify:%.2f%%\n", i, correct, total, (float)proposals/(i+1), avg_iou*100/total, 100.*correct/total, 100.*class_correct/total);
        free(id);
        free_image(orig);
        free_image(sized);
    }
}

void test_yolo(char *datacfg, char *cfgfile, char *weightfile, char *filename, float thresh)
{
    list *options = read_data_cfg(datacfg);
    
    char *train_list = option_find_str(options, "train", "data/train_list.txt");
    char *test_list = option_find_str(options, "test", "data/test_list.txt");
    char *valid_list = option_find_str(options, "valid", "data/valid_list.txt");
    
    //char *backup_directory = option_find_str(options, "backup", "/backup/");
    //char *label_list = option_find_str(options, "labels", "data/labels_list.txt");
    
    //int classes = option_find_int(options, "classes", 2);
    
    //char **labels = get_labels(label_list);

    network net = parse_network_cfg(cfgfile);
    if(weightfile){
        load_weights(&net, weightfile);
    }
    detection_layer l = net.layers[net.n-1];
    set_batch_network(&net, 1);
    srand(2222222);
    clock_t time;
    char buff[256];
    char *input = buff;
    int j;
    float nms=.5;
    box *boxes = calloc(l.side*l.side*l.n, sizeof(box));
    float **probs = calloc(l.side*l.side*l.n, sizeof(float *));
    for(j = 0; j < l.side*l.side*l.n; ++j) probs[j] = calloc(l.classes, sizeof(float *));
    while(1){
        if(filename){
            strncpy(input, filename, 256);
        } else {
            printf("Enter Image Path: ");
            fflush(stdout);
            input = fgets(input, 256, stdin);
            if(!input) return;
            strtok(input, "\n");
        }
        image im = load_image_color(input,0,0);
        image sized = resize_image(im, net.w, net.h);
        float *X = sized.data;
        time=clock();
        float *predictions = network_predict(net, X);
        printf("%s: Predicted in %f seconds.\n", input, sec(clock()-time));
        convert_yolo_detections(predictions, l.classes, l.n, l.sqrt, l.side, 1, 1, thresh, probs, boxes, 0);
        if (nms) do_nms_sort(boxes, probs, l.side*l.side*l.n, l.classes, nms);
        //draw_detections(im, l.side*l.side*l.n, thresh, boxes, probs, voc_names, voc_labels, 20);
        draw_detections(im, l.side*l.side*l.n, thresh, boxes, probs, voc_names, voc_labels, l.classes);
        save_image(im, "predictions");
        show_image(im, "predictions");

        show_image(sized, "resized");
        free_image(im);
        free_image(sized);
#ifdef OPENCV
        cvWaitKey(0);
        cvDestroyAllWindows();
#endif
        if (filename) break;
    }
}

void demo_yolo(char *cfgfile, char *weightfile, float thresh, int cam_index, char *filename);

void run_yolo(int argc, char **argv)
{
    int i;
    for(i = 0; i < 20; ++i){
        char buff[256];
        sprintf(buff, "data/labels/%s.png", voc_names[i]);
        voc_labels[i] = load_image_color(buff, 0, 0);
    }

    float thresh = find_float_arg(argc, argv, "-thresh", .2);
    int cam_index = find_int_arg(argc, argv, "-c", 0);
    if(argc < 4){
        fprintf(stderr, "usage: %s %s [train/test/valid] [cfg] [weights (optional)]\n", argv[0], argv[1]);
        return;
    }

    char *data = argv[3];
    
    char *cfg = argv[4];
    char *weights = (argc > 5) ? argv[5] : 0;
    char *filename = (argc > 6) ? argv[6]: 0;
    if(0==strcmp(argv[2], "test")) test_yolo(data, cfg, weights, filename, thresh);
    else if(0==strcmp(argv[2], "train")) train_yolo(data, cfg, weights);
    else if(0==strcmp(argv[2], "valid")) validate_yolo(data, cfg, weights);
    else if(0==strcmp(argv[2], "recall")) validate_yolo_recall(data, cfg, weights);
    else if(0==strcmp(argv[2], "recall_classify")) validate_yolo_classify(data, cfg, weights);
    else if(0==strcmp(argv[2], "demo")) demo_yolo(cfg, weights, thresh, cam_index, filename);
}
