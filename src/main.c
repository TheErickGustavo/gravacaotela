#include <gtk/gtk.h>
#include <gio/gio.h>
#include <glib/gstdio.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    GtkWidget *window;
    GtkWidget *status_label;
    GtkWidget *area_label;
    GtkWidget *fps_combo;
    GtkWidget *quality_combo;
    GtkWidget *audio_check;
    GtkWidget *cursor_check;
    GtkWidget *start_button;
    GtkWidget *stop_button;
    gboolean region_selected;
    gint region_x;
    gint region_y;
    gint region_w;
    gint region_h;
    gboolean closing;
    GSubprocess *process;
    gchar *output_path;
} AppState;

static void set_status(AppState *state, const gchar *message)
{
    gtk_label_set_text(GTK_LABEL(state->status_label), message);
}

static void add_arg(GPtrArray *args, const gchar *value)
{
    g_ptr_array_add(args, g_strdup(value));
}

static gchar *get_videos_directory(void)
{
    const gchar *special = g_get_user_special_dir(G_USER_DIRECTORY_VIDEOS);
    if (special && g_file_test(special, G_FILE_TEST_IS_DIR))
        return g_strdup(special);

    const gchar *home = g_get_home_dir();
    gchar *candidate = g_build_filename(home, "Vídeos", NULL);
    if (g_file_test(candidate, G_FILE_TEST_IS_DIR))
        return candidate;
    g_free(candidate);

    candidate = g_build_filename(home, "Videos", NULL);
    if (g_file_test(candidate, G_FILE_TEST_IS_DIR))
        return candidate;
    g_free(candidate);

    return g_strdup(home);
}

static gchar *choose_output_file(AppState *state)
{
    GtkWidget *dialog = gtk_file_chooser_dialog_new(
        "Salvar gravação MP4",
        GTK_WINDOW(state->window),
        GTK_FILE_CHOOSER_ACTION_SAVE,
        "_Cancelar", GTK_RESPONSE_CANCEL,
        "_Salvar", GTK_RESPONSE_ACCEPT,
        NULL);

    gtk_file_chooser_set_do_overwrite_confirmation(GTK_FILE_CHOOSER(dialog), TRUE);

    GDateTime *now = g_date_time_new_now_local();
    gchar *stamp = g_date_time_format(now, "%Y-%m-%d_%H-%M-%S");
    gchar *default_name = g_strdup_printf("gravacao-%s.mp4", stamp);
    gtk_file_chooser_set_current_name(GTK_FILE_CHOOSER(dialog), default_name);

    gchar *directory = get_videos_directory();
    if (g_file_test(directory, G_FILE_TEST_IS_DIR))
        gtk_file_chooser_set_current_folder(GTK_FILE_CHOOSER(dialog), directory);

    gint response = gtk_dialog_run(GTK_DIALOG(dialog));
    gchar *path = NULL;
    if (response == GTK_RESPONSE_ACCEPT)
        path = gtk_file_chooser_get_filename(GTK_FILE_CHOOSER(dialog));

    gtk_widget_destroy(dialog);
    g_date_time_unref(now);
    g_free(stamp);
    g_free(default_name);
    g_free(directory);

    if (!path)
        return NULL;

    gchar *lower = g_ascii_strdown(path, -1);
    gboolean already_mp4 = g_str_has_suffix(lower, ".mp4");
    g_free(lower);
    if (!already_mp4) {
        gchar *with_extension = g_strconcat(path, ".mp4", NULL);
        g_free(path);
        path = with_extension;
    }

    return path;
}

static void choose_region(GtkButton *button, gpointer user_data)
{
    (void)button;
    AppState *state = user_data;

    gchar *argv[] = { "slop", "-f", "%x %y %w %h", NULL };
    gchar *stdout_text = NULL;
    gchar *stderr_text = NULL;
    gint wait_status = 0;
    GError *error = NULL;

    gboolean spawned = g_spawn_sync(
        NULL, argv, NULL, G_SPAWN_SEARCH_PATH,
        NULL, NULL, &stdout_text, &stderr_text, &wait_status, &error);

    if (!spawned) {
        gchar *message = g_strdup_printf(
            "Não consegui iniciar o seletor de área (slop). Instale com: sudo apt install slop\nDetalhe: %s",
            error ? error->message : "erro desconhecido");
        set_status(state, message);
        g_free(message);
        g_clear_error(&error);
        g_free(stdout_text);
        g_free(stderr_text);
        return;
    }

    if (wait_status != 0 || !stdout_text) {
        set_status(state, "Seleção cancelada; a área anterior foi mantida.");
        g_free(stdout_text);
        g_free(stderr_text);
        return;
    }

    gint x = 0, y = 0, width = 0, height = 0;
    if (sscanf(stdout_text, "%d %d %d %d", &x, &y, &width, &height) != 4 ||
        width < 2 || height < 2) {
        set_status(state, "O seletor não retornou uma região válida. Tente novamente.");
        g_free(stdout_text);
        g_free(stderr_text);
        return;
    }

    /* H.264 com yuv420p precisa de largura e altura pares. */
    width &= ~1;
    height &= ~1;
    if (width < 2 || height < 2) {
        set_status(state, "A região é pequena demais. Selecione uma área maior.");
        g_free(stdout_text);
        g_free(stderr_text);
        return;
    }

    state->region_selected = TRUE;
    state->region_x = x;
    state->region_y = y;
    state->region_w = width;
    state->region_h = height;

    gchar *area = g_strdup_printf(
        "Região: %d × %d px  •  posição %d, %d",
        width, height, x, y);
    gtk_label_set_text(GTK_LABEL(state->area_label), area);
    set_status(state, "Área selecionada. Escolha FPS/qualidade e inicie a gravação.");

    g_free(area);
    g_free(stdout_text);
    g_free(stderr_text);
}

static void choose_fullscreen(GtkButton *button, gpointer user_data)
{
    (void)button;
    AppState *state = user_data;
    state->region_selected = FALSE;
    gtk_label_set_text(GTK_LABEL(state->area_label), "Área: tela inteira");
    set_status(state, "A captura usará a tela inteira do display X11.");
}

static gchar *get_system_audio_monitor(GError **error_out)
{
    gchar *argv[] = { "pactl", "get-default-sink", NULL };
    gchar *stdout_text = NULL;
    gchar *stderr_text = NULL;
    gint wait_status = 0;
    GError *error = NULL;

    gboolean spawned = g_spawn_sync(
        NULL, argv, NULL, G_SPAWN_SEARCH_PATH,
        NULL, NULL, &stdout_text, &stderr_text, &wait_status, &error);

    g_free(stderr_text);
    if (!spawned || wait_status != 0 || !stdout_text) {
        if (error) {
            g_propagate_error(error_out, error);
        } else {
            g_set_error(error_out, G_IO_ERROR, G_IO_ERROR_FAILED,
                        "Não foi possível descobrir a saída de áudio padrão. Verifique se pactl está instalado e se o áudio do sistema está ativo.");
        }
        g_free(stdout_text);
        return NULL;
    }

    gchar *sink = g_strstrip(stdout_text);
    if (!*sink) {
        g_set_error(error_out, G_IO_ERROR, G_IO_ERROR_FAILED,
                    "O servidor de áudio não informou uma saída padrão.");
        g_free(stdout_text);
        return NULL;
    }

    gchar *monitor = g_strconcat(sink, ".monitor", NULL);
    g_free(stdout_text);
    return monitor;
}

static gchar *read_process_stderr(GSubprocess *process)
{
    GInputStream *stream = g_subprocess_get_stderr_pipe(process);
    if (!stream)
        return g_strdup("");

    GString *output = g_string_new(NULL);
    while (output->len < 4096) {
        GError *error = NULL;
        GBytes *bytes = g_input_stream_read_bytes(stream, 512, NULL, &error);
        if (!bytes) {
            g_clear_error(&error);
            break;
        }

        gsize size = 0;
        const gchar *data = g_bytes_get_data(bytes, &size);
        if (size == 0) {
            g_bytes_unref(bytes);
            break;
        }

        gsize room = 4096 - output->len;
        g_string_append_len(output, data, (gssize)MIN(size, room));
        g_bytes_unref(bytes);
    }

    return g_string_free(output, FALSE);
}

static void on_process_wait(GObject *source_object, GAsyncResult *result, gpointer user_data)
{
    AppState *state = user_data;
    GSubprocess *process = G_SUBPROCESS(source_object);
    GError *error = NULL;
    gboolean waited = g_subprocess_wait_finish(process, result, &error);
    gboolean successful = waited && g_subprocess_get_successful(process);
    gchar *stderr_text = read_process_stderr(process);

    gtk_widget_set_sensitive(state->start_button, TRUE);
    gtk_widget_set_sensitive(state->stop_button, FALSE);

    if (successful) {
        gchar *message = g_strdup_printf("Gravação concluída:\n%s",
                                          state->output_path ? state->output_path : "arquivo MP4");
        set_status(state, message);
        g_free(message);
    } else {
        gchar *details = g_strstrip(stderr_text);
        if (!*details && error)
            details = error->message;
        if (!*details)
            details = "Verifique o FFmpeg, o display X11 e as permissões de captura.";
        gchar *message = g_strdup_printf("A gravação terminou com erro:\n%.450s", details);
        set_status(state, message);
        g_free(message);
    }

    g_free(stderr_text);
    g_clear_error(&error);
    g_clear_object(&state->process);

    if (state->closing)
        gtk_window_close(GTK_WINDOW(state->window));
}

static void stop_recording(AppState *state)
{
    if (!state->process)
        return;

    GOutputStream *input = g_subprocess_get_stdin_pipe(state->process);
    GError *error = NULL;
    if (input) {
        gsize written = 0;
        if (!g_output_stream_write_all(input, "q\n", 2, &written, NULL, &error)) {
            gchar *message = g_strdup_printf("Solicitei a parada, mas não consegui enviar o comando ao FFmpeg: %s",
                                              error ? error->message : "erro de escrita");
            set_status(state, message);
            g_free(message);
            g_clear_error(&error);
            return;
        }
        g_output_stream_flush(input, NULL, NULL);
    }

    gtk_widget_set_sensitive(state->stop_button, FALSE);
    set_status(state, "Finalizando o MP4 e salvando o arquivo…");
}

static void stop_clicked(GtkButton *button, gpointer user_data)
{
    (void)button;
    stop_recording((AppState *)user_data);
}

static void start_recording(GtkButton *button, gpointer user_data)
{
    (void)button;
    AppState *state = user_data;
    if (state->process)
        return;

    gchar *path = choose_output_file(state);
    if (!path)
        return;

    gint fps = 15;
    gchar *fps_text = gtk_combo_box_text_get_active_text(GTK_COMBO_BOX_TEXT(state->fps_combo));
    if (fps_text) {
        fps = atoi(fps_text);
        g_free(fps_text);
    }
    if (fps != 10 && fps != 15 && fps != 30)
        fps = 15;

    gint quality_index = gtk_combo_box_get_active(GTK_COMBO_BOX(state->quality_combo));
    gint crf = (quality_index == 0) ? 30 : (quality_index == 2 ? 24 : 27);
    gboolean capture_audio = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(state->audio_check));
    gboolean capture_cursor = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(state->cursor_check));

    gchar *audio_monitor = NULL;
    if (capture_audio) {
        GError *audio_error = NULL;
        audio_monitor = get_system_audio_monitor(&audio_error);
        if (!audio_monitor) {
            gchar *message = g_strdup_printf("Não consegui preparar o áudio do sistema: %s",
                                              audio_error ? audio_error->message : "erro desconhecido");
            set_status(state, message);
            g_free(message);
            g_clear_error(&audio_error);
            g_free(path);
            return;
        }
    }

    const gchar *display_env = g_getenv("DISPLAY");
    const gchar *display = (display_env && *display_env) ? display_env : ":0.0";
    gchar *input_display = state->region_selected
        ? g_strdup_printf("%s+%d,%d", display, state->region_x, state->region_y)
        : g_strdup(display);

    GPtrArray *args = g_ptr_array_new_with_free_func(g_free);
    add_arg(args, "ffmpeg");
    add_arg(args, "-hide_banner");
    add_arg(args, "-nostats");
    add_arg(args, "-loglevel");
    add_arg(args, "error");
    add_arg(args, "-y");
    add_arg(args, "-f");
    add_arg(args, "x11grab");
    add_arg(args, "-framerate");
    gchar *fps_value = g_strdup_printf("%d", fps);
    add_arg(args, fps_value);
    g_free(fps_value);
    add_arg(args, "-draw_mouse");
    add_arg(args, capture_cursor ? "1" : "0");

    if (state->region_selected) {
        add_arg(args, "-video_size");
        gchar *size = g_strdup_printf("%dx%d", state->region_w, state->region_h);
        add_arg(args, size);
        g_free(size);
    }

    add_arg(args, "-i");
    add_arg(args, input_display);

    if (audio_monitor) {
        add_arg(args, "-thread_queue_size");
        add_arg(args, "512");
        add_arg(args, "-f");
        add_arg(args, "pulse");
        add_arg(args, "-i");
        add_arg(args, audio_monitor);
        add_arg(args, "-map");
        add_arg(args, "0:v:0");
        add_arg(args, "-map");
        add_arg(args, "1:a:0");
    } else {
        add_arg(args, "-an");
    }

    add_arg(args, "-c:v");
    add_arg(args, "libx264");
    add_arg(args, "-preset");
    add_arg(args, "ultrafast");
    add_arg(args, "-tune");
    add_arg(args, "zerolatency");
    add_arg(args, "-crf");
    gchar *crf_value = g_strdup_printf("%d", crf);
    add_arg(args, crf_value);
    g_free(crf_value);
    add_arg(args, "-pix_fmt");
    add_arg(args, "yuv420p");

    if (audio_monitor) {
        add_arg(args, "-c:a");
        add_arg(args, "aac");
        add_arg(args, "-b:a");
        add_arg(args, "96k");
    }

    add_arg(args, "-movflags");
    add_arg(args, "+faststart");
    add_arg(args, "-metadata");
    add_arg(args, "title=Gravação de tela");
    add_arg(args, path);
    g_ptr_array_add(args, NULL);

    GError *error = NULL;
    GSubprocessFlags flags = G_SUBPROCESS_FLAGS_STDIN_PIPE |
                             G_SUBPROCESS_FLAGS_STDERR_PIPE |
                             G_SUBPROCESS_FLAGS_STDOUT_SILENCE;
    GSubprocess *process = g_subprocess_newv(
        (const gchar * const *)args->pdata, flags, &error);

    g_ptr_array_unref(args);
    g_free(input_display);
    g_free(audio_monitor);

    if (!process) {
        gchar *message = g_strdup_printf("Não consegui iniciar o FFmpeg: %s",
                                          error ? error->message : "erro desconhecido");
        set_status(state, message);
        g_free(message);
        g_clear_error(&error);
        g_free(path);
        return;
    }

    g_clear_pointer(&state->output_path, g_free);
    state->output_path = path;
    state->process = process;

    gtk_widget_set_sensitive(state->start_button, FALSE);
    gtk_widget_set_sensitive(state->stop_button, TRUE);
    gchar *message = g_strdup_printf("Gravando a %d FPS. Salve com Parar para finalizar o MP4.", fps);
    set_status(state, message);
    g_free(message);

    g_subprocess_wait_async(process, NULL, on_process_wait, state);
}

static void open_output_folder(GtkButton *button, gpointer user_data)
{
    (void)button;
    AppState *state = user_data;
    gchar *folder = state->output_path
        ? g_path_get_dirname(state->output_path)
        : get_videos_directory();

    gchar *argv[] = { "xdg-open", folder, NULL };
    GError *error = NULL;
    if (!g_spawn_async(NULL, argv, NULL, G_SPAWN_SEARCH_PATH,
                       NULL, NULL, NULL, &error)) {
        gchar *message = g_strdup_printf("Não consegui abrir a pasta: %s",
                                          error ? error->message : "erro desconhecido");
        set_status(state, message);
        g_free(message);
        g_clear_error(&error);
    }
    g_free(folder);
}

static gboolean on_window_delete(GtkWidget *widget, GdkEvent *event, gpointer user_data)
{
    (void)widget;
    (void)event;
    AppState *state = user_data;

    if (state->process) {
        state->closing = TRUE;
        stop_recording(state);
        return TRUE;
    }
    return FALSE;
}

static void activate(GtkApplication *app, gpointer user_data)
{
    AppState *state = user_data;

    GtkWidget *window = gtk_application_window_new(app);
    state->window = window;
    gtk_window_set_title(GTK_WINDOW(window), "GravaçãoTela");
    gtk_window_set_default_size(GTK_WINDOW(window), 540, 500);
    gtk_window_set_resizable(GTK_WINDOW(window), FALSE);
    gtk_container_set_border_width(GTK_CONTAINER(window), 18);

    GtkWidget *main_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 12);
    gtk_container_add(GTK_CONTAINER(window), main_box);

    GtkWidget *title = gtk_label_new(NULL);
    gtk_label_set_markup(GTK_LABEL(title), "<span size='x-large' weight='bold'>GravaçãoTela</span>");
    gtk_widget_set_halign(title, GTK_ALIGN_START);
    gtk_box_pack_start(GTK_BOX(main_box), title, FALSE, FALSE, 0);

    GtkWidget *description = gtk_label_new(
        "Gravador enxuto para Linux Mint XFCE. Captura X11 com FFmpeg e salva em MP4.");
    gtk_label_set_line_wrap(GTK_LABEL(description), TRUE);
    gtk_widget_set_halign(description, GTK_ALIGN_START);
    gtk_box_pack_start(GTK_BOX(main_box), description, FALSE, FALSE, 0);

    GtkWidget *area_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    GtkWidget *region_button = gtk_button_new_with_label("Selecionar região…");
    GtkWidget *fullscreen_button = gtk_button_new_with_label("Tela inteira");
    state->area_label = gtk_label_new("Área: tela inteira");
    gtk_label_set_line_wrap(GTK_LABEL(state->area_label), TRUE);
    gtk_label_set_xalign(GTK_LABEL(state->area_label), 0.0f);
    gtk_box_pack_start(GTK_BOX(area_box), region_button, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(area_box), fullscreen_button, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(main_box), area_box, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(main_box), state->area_label, FALSE, FALSE, 0);
    g_signal_connect(region_button, "clicked", G_CALLBACK(choose_region), state);
    g_signal_connect(fullscreen_button, "clicked", G_CALLBACK(choose_fullscreen), state);

    GtkWidget *grid = gtk_grid_new();
    gtk_grid_set_row_spacing(GTK_GRID(grid), 10);
    gtk_grid_set_column_spacing(GTK_GRID(grid), 14);
    gtk_widget_set_margin_top(grid, 4);
    gtk_box_pack_start(GTK_BOX(main_box), grid, FALSE, FALSE, 0);

    GtkWidget *fps_label = gtk_label_new("Taxa de quadros");
    gtk_widget_set_halign(fps_label, GTK_ALIGN_START);
    state->fps_combo = gtk_combo_box_text_new();
    gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(state->fps_combo), "10 FPS");
    gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(state->fps_combo), "15 FPS");
    gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(state->fps_combo), "30 FPS");
    gtk_combo_box_set_active(GTK_COMBO_BOX(state->fps_combo), 1);
    gtk_grid_attach(GTK_GRID(grid), fps_label, 0, 0, 1, 1);
    gtk_grid_attach(GTK_GRID(grid), state->fps_combo, 1, 0, 1, 1);

    GtkWidget *quality_label = gtk_label_new("Compressão");
    gtk_widget_set_halign(quality_label, GTK_ALIGN_START);
    state->quality_combo = gtk_combo_box_text_new();
    gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(state->quality_combo), "Arquivo menor (CRF 30)");
    gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(state->quality_combo), "Equilibrado (CRF 27)");
    gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(state->quality_combo), "Mais nítido (CRF 24)");
    gtk_combo_box_set_active(GTK_COMBO_BOX(state->quality_combo), 1);
    gtk_grid_attach(GTK_GRID(grid), quality_label, 0, 1, 1, 1);
    gtk_grid_attach(GTK_GRID(grid), state->quality_combo, 1, 1, 1, 1);

    state->cursor_check = gtk_check_button_new_with_label("Incluir cursor do mouse");
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(state->cursor_check), TRUE);
    gtk_box_pack_start(GTK_BOX(main_box), state->cursor_check, FALSE, FALSE, 0);

    state->audio_check = gtk_check_button_new_with_label("Gravar áudio do sistema (opcional)");
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(state->audio_check), FALSE);
    gtk_box_pack_start(GTK_BOX(main_box), state->audio_check, FALSE, FALSE, 0);

    GtkWidget *buttons = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    state->start_button = gtk_button_new_with_label("●  Iniciar gravação");
    state->stop_button = gtk_button_new_with_label("■  Parar e salvar");
    GtkWidget *folder_button = gtk_button_new_with_label("Abrir pasta");
    gtk_widget_set_hexpand(state->start_button, TRUE);
    gtk_widget_set_hexpand(state->stop_button, TRUE);
    gtk_box_pack_start(GTK_BOX(buttons), state->start_button, TRUE, TRUE, 0);
    gtk_box_pack_start(GTK_BOX(buttons), state->stop_button, TRUE, TRUE, 0);
    gtk_box_pack_start(GTK_BOX(buttons), folder_button, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(main_box), buttons, FALSE, FALSE, 0);

    state->status_label = gtk_label_new(
        "Pronto. Selecione uma região ou grave a tela inteira.");
    gtk_label_set_line_wrap(GTK_LABEL(state->status_label), TRUE);
    gtk_label_set_max_width_chars(GTK_LABEL(state->status_label), 68);
    gtk_label_set_xalign(GTK_LABEL(state->status_label), 0.0f);
    gtk_widget_set_margin_top(state->status_label, 4);
    gtk_box_pack_start(GTK_BOX(main_box), state->status_label, FALSE, FALSE, 0);

    GtkWidget *footnote = gtk_label_new(
        "Perfil padrão: H.264 ultrafast, 15 FPS, sem áudio. Projetado para sessões X11.");
    gtk_label_set_line_wrap(GTK_LABEL(footnote), TRUE);
    gtk_widget_set_halign(footnote, GTK_ALIGN_START);
    gtk_widget_set_opacity(footnote, 0.78);
    gtk_box_pack_start(GTK_BOX(main_box), footnote, FALSE, FALSE, 0);

    gtk_widget_set_sensitive(state->stop_button, FALSE);
    g_signal_connect(state->start_button, "clicked", G_CALLBACK(start_recording), state);
    g_signal_connect(state->stop_button, "clicked", G_CALLBACK(stop_clicked), state);
    g_signal_connect(folder_button, "clicked", G_CALLBACK(open_output_folder), state);
    g_signal_connect(window, "delete-event", G_CALLBACK(on_window_delete), state);

    gtk_widget_show_all(window);
}

int main(int argc, char **argv)
{
    AppState state = {0};
    GtkApplication *app = gtk_application_new(
        "org.theerickgustavo.gravacaotela",
        G_APPLICATION_DEFAULT_FLAGS);

    g_signal_connect(app, "activate", G_CALLBACK(activate), &state);
    int result = g_application_run(G_APPLICATION(app), argc, argv);

    if (state.process) {
        /* A rotina normal de fechamento pede ao FFmpeg que finalize o arquivo. */
        stop_recording(&state);
        g_clear_object(&state.process);
    }

    g_clear_pointer(&state.output_path, g_free);
    g_object_unref(app);
    return result;
}
