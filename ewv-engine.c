#include <gtk/gtk.h>
#include <gtk4-layer-shell.h>
#include <json-glib/json-glib.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// --- STRUTTURE DATI ---
typedef struct {
    int x, y, w, h, layer;
    char *css_path;
    JsonObject *tree;
} WidgetData;

typedef struct {
    GtkWidget *label;
    char *format_string;
    char *command;
} PollData;

// --- GESTIONE VIDEO ---
static void on_video_finished(GtkMediaStream *stream, GParamSpec *pspec, gpointer user_data) {
    if (gtk_media_stream_get_ended(stream)) {
        gtk_media_stream_seek(stream, 0);
        gtk_media_stream_play(stream);
    }
}

// --- GESTIONE STILE CSS ---
static void apply_css(const char *path) {
    GtkCssProvider *provider = gtk_css_provider_new();
    gtk_css_provider_load_from_path(provider, path);
    gtk_style_context_add_provider_for_display(
        gdk_display_get_default(),
        GTK_STYLE_PROVIDER(provider),
        GTK_STYLE_PROVIDER_PRIORITY_APPLICATION
    );
}

// =========================================================================
// MOTORE DI AGGIORNAMENTO DINAMICO (POLLING)
// =========================================================================
static gboolean update_label_poll(gpointer user_data) {
    PollData *pd = (PollData *)user_data;
    FILE *fp;
    char buffer[128];

    // Eseguiamo lo script bash in background
    fp = popen(pd->command, "r");
    if (fp == NULL) return G_SOURCE_CONTINUE;

    if (fgets(buffer, sizeof(buffer), fp) != NULL) {
        buffer[strcspn(buffer, "\n")] = 0; // Rimuove accapo finale
        
        char final_text[256];
        snprintf(final_text, sizeof(final_text), pd->format_string, buffer);
        
        gtk_label_set_text(GTK_LABEL(pd->label), final_text);
    }
    pclose(fp);
    
    return G_SOURCE_CONTINUE;
}

// =========================================================================
// GESTIONE CLICK BOTTONI
// =========================================================================
static void on_button_clicked(GtkButton *button, gpointer user_data) {
    char *command = (char *)user_data;
    if (command) {
        // Aggiungiamo & alla fine per eseguirlo in background e non bloccare Wayland
        char full_cmd[512];
        snprintf(full_cmd, sizeof(full_cmd), "%s &", command);
        system(full_cmd);
    }
}

// =========================================================================
// COSTRUTTORE DELL'ALBERO E DEI WIDGET
// =========================================================================
static GtkWidget* build_widget_from_json(JsonObject *node) {
    if (!node) return NULL;

    const char *type = json_object_has_member(node, "type") ? 
                       json_object_get_string_member(node, "type") : NULL;
    
    if (!type) return NULL;

    // --- SE È UNA SCATOLA (BOX) ---
    if (g_strcmp0(type, "box") == 0) {
        GtkOrientation orientation = GTK_ORIENTATION_HORIZONTAL;
        if (json_object_has_member(node, "orientation")) {
            if (g_strcmp0(json_object_get_string_member(node, "orientation"), "vertical") == 0) {
                orientation = GTK_ORIENTATION_VERTICAL;
            }
        }
        
        // Lettura dinamica dello spacing
        gint spacing = 0; 
        if (json_object_has_member(node, "spacing")) {
            spacing = json_object_get_int_member(node, "spacing");
        }
        
        GtkWidget *box = gtk_box_new(orientation, spacing);
        gtk_widget_set_valign(box, GTK_ALIGN_CENTER);
        gtk_widget_set_halign(box, GTK_ALIGN_CENTER);

        if (json_object_has_member(node, "class")) {
            gtk_widget_add_css_class(box, json_object_get_string_member(node, "class"));
        }

        if (json_object_has_member(node, "children")) {
            JsonArray *children = json_object_get_array_member(node, "children");
            for (guint i = 0; i < json_array_get_length(children); i++) {
                GtkWidget *child = build_widget_from_json(json_array_get_object_element(children, i));
                if (child) gtk_box_append(GTK_BOX(box), child);
            }
        }
        return box;
    } 
    // --- SE È UN TESTO (LABEL) ---
    else if (g_strcmp0(type, "label") == 0) {
        const char *text = json_object_has_member(node, "text") ? 
                           json_object_get_string_member(node, "text") : "";
        GtkWidget *label = gtk_label_new(text);

        // -- RILEVAMENTO VARIABILI DINAMICHE --
        if (strstr(text, "${cpu}") != NULL) {
            PollData *pd = g_malloc(sizeof(PollData));
            pd->label = label;
            pd->format_string = g_strdup("🧠 CPU: %s%%");
            pd->command = g_strdup("bash -c '~/.config/ewv/scripts/cpu.sh 2>/dev/null || ~/.config/eww/scripts/cpu.sh 2>/dev/null || echo \"0\"'");
            g_timeout_add(1000, update_label_poll, pd);
            update_label_poll(pd); // Primo avvio immediato!
        } 
        else if (strstr(text, "${ram}") != NULL) {
            PollData *pd = g_malloc(sizeof(PollData));
            pd->label = label;
            pd->format_string = g_strdup("🧬 RAM: %s%%");
            pd->command = g_strdup("bash -c '~/.config/ewv/scripts/ram.sh 2>/dev/null || ~/.config/eww/scripts/ram.sh 2>/dev/null || free -m | awk \"/Mem:/ {printf \\\"%.0f\\\", \\$3/\\$2 * 100.0}\"'");
            g_timeout_add(2000, update_label_poll, pd);
            update_label_poll(pd); // Primo avvio immediato!
        }
        else if (strstr(text, "${weather}") != NULL) {
            PollData *pd = g_malloc(sizeof(PollData));
            pd->label = label;
            pd->format_string = g_strdup("%s Lonigo");
            pd->command = g_strdup("bash -c '~/.config/ewv/scripts/weather.sh 2>/dev/null || ~/.config/eww/scripts/weather.sh 2>/dev/null || echo \"☀️ --°C\"'");
            g_timeout_add(600000, update_label_poll, pd); // 10 Minuti!
            update_label_poll(pd); // Primo avvio immediato!
        }
        
        return label;
    } 
    // --- SE È UN'IMMAGINE (IMAGE) ---
    else if (g_strcmp0(type, "image") == 0) {
        const char *path = json_object_has_member(node, "path") ? 
                           json_object_get_string_member(node, "path") : "";
        GFile *file = g_file_new_for_path(path);
        GtkWidget *picture = gtk_picture_new_for_file(file);
        g_object_unref(file);

        gtk_picture_set_can_shrink(GTK_PICTURE(picture), TRUE);
        gtk_picture_set_content_fit(GTK_PICTURE(picture), GTK_CONTENT_FIT_CONTAIN);
        
        if (json_object_has_member(node, "image-width") && json_object_has_member(node, "image-height")) {
            int img_w = json_object_get_int_member(node, "image-width");
            int img_h = json_object_get_int_member(node, "image-height");
            gtk_widget_set_size_request(picture, img_w, img_h);
        }
        return picture;
    }
    // --- SE È UN VIDEO ---
    else if (g_strcmp0(type, "video") == 0) {
        const char *path = json_object_has_member(node, "path") ? 
                           json_object_get_string_member(node, "path") : "";
        GFile *file = g_file_new_for_path(path);
        GtkWidget *video = gtk_video_new_for_file(file);
        
        GtkMediaStream *stream = gtk_video_get_media_stream(GTK_VIDEO(video));
        g_signal_connect(stream, "notify::ended", G_CALLBACK(on_video_finished), NULL);
        gtk_video_set_autoplay(GTK_VIDEO(video), TRUE);
        g_object_unref(file);
        return video;
    }
    // --- SE È UN BOTTONE (BUTTON) ---
    else if (g_strcmp0(type, "button") == 0) {
        GtkWidget *button = gtk_button_new();
        
        // Aggiungiamo classi CSS per lo stile
        gtk_widget_add_css_class(button, "flat"); 
        if (json_object_has_member(node, "class")) {
            gtk_widget_add_css_class(button, json_object_get_string_member(node, "class"));
        }

        // Colleghiamo il comando onclick
        if (json_object_has_member(node, "onclick")) {
            const char *cmd = json_object_get_string_member(node, "onclick");
            g_signal_connect(button, "clicked", G_CALLBACK(on_button_clicked), g_strdup(cmd));
        }

        // Aggiungiamo i figli (icone, testi, ecc.)
        if (json_object_has_member(node, "children")) {
            JsonArray *children = json_object_get_array_member(node, "children");
            if (json_array_get_length(children) > 0) {
                GtkWidget *child = build_widget_from_json(json_array_get_object_element(children, 0));
                if (child) gtk_button_set_child(GTK_BUTTON(button), child);
            }
        } 
        else if (json_object_has_member(node, "text")) {
            gtk_button_set_label(GTK_BUTTON(button), json_object_get_string_member(node, "text"));
        }

        return button;
    }

    return NULL;
}

// =========================================================================
// AVVIO APPLICAZIONE GTK LAYER SHELL
// =========================================================================
static void activate(GtkApplication *app, gpointer user_data) {
    WidgetData *data = (WidgetData *)user_data;

    GtkWidget *win = gtk_application_window_new(app);
    gtk_window_set_default_size(GTK_WINDOW(win), data->w, data->h);
    gtk_window_set_decorated(GTK_WINDOW(win), FALSE);

    gtk_layer_init_for_window(GTK_WINDOW(win));
    gtk_layer_set_layer(GTK_WINDOW(win), (GtkLayerShellLayer)data->layer);
    
    gtk_layer_set_anchor(GTK_WINDOW(win), GTK_LAYER_SHELL_EDGE_TOP, TRUE);
    gtk_layer_set_anchor(GTK_WINDOW(win), GTK_LAYER_SHELL_EDGE_LEFT, TRUE);
    gtk_layer_set_margin(GTK_WINDOW(win), GTK_LAYER_SHELL_EDGE_TOP, data->y);
    gtk_layer_set_margin(GTK_WINDOW(win), GTK_LAYER_SHELL_EDGE_LEFT, data->x);

    if (data->css_path && strlen(data->css_path) > 0) {
        apply_css(data->css_path);
    }

    if (data->tree) {
        GtkWidget *main_widget = build_widget_from_json(data->tree);
        if (main_widget) {
            gtk_window_set_child(GTK_WINDOW(win), main_widget);
        }
    }

    gtk_window_present(GTK_WINDOW(win));
}

int main(int argc, char **argv) {
    if (argc < 2) return 1;

    JsonParser *parser = json_parser_new();
    if (!json_parser_load_from_data(parser, argv[1], -1, NULL)) {
        g_printerr("[ERRORE C] Impossibile parsare il JSON passato da Python.\n");
        return 1;
    }
    
    JsonObject *obj = json_node_get_object(json_parser_get_root(parser));
    WidgetData data;

    data.w = json_object_has_member(obj, "w") ? json_object_get_int_member(obj, "w") : 300;
    data.h = json_object_has_member(obj, "h") ? json_object_get_int_member(obj, "h") : 260;
    data.x = json_object_has_member(obj, "x") ? json_object_get_int_member(obj, "x") : 0;
    data.y = json_object_has_member(obj, "y") ? json_object_get_int_member(obj, "y") : 0;
    data.layer = json_object_has_member(obj, "layer") ? json_object_get_int_member(obj, "layer") : 0;
    
    data.css_path = json_object_has_member(obj, "css_path") ? 
                    g_strdup(json_object_get_string_member(obj, "css_path")) : NULL;
    
    data.tree = json_object_has_member(obj, "tree") ? 
                json_object_get_object_member(obj, "tree") : NULL;

    const char *app_id_str = json_object_has_member(obj, "app_id") ? 
                             json_object_get_string_member(obj, "app_id") : "com.matti.ewv.default";
    char *app_id = g_strdup(app_id_str);

    GtkApplication *app = gtk_application_new(app_id, G_APPLICATION_DEFAULT_FLAGS);
    g_signal_connect(app, "activate", G_CALLBACK(activate), &data);

    int status = g_application_run(G_APPLICATION(app), 0, NULL);
    
    g_free(data.css_path); 
    g_free(app_id);
    g_object_unref(parser); 
    g_object_unref(app);
    return status;
}
